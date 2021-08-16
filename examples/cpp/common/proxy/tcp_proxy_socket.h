#pragma once

namespace proxy
{
	enum class connection_status
	{
		client_no_change = 0,
		client_connected,
		client_established,
		client_completed,
	};

	template <typename T>
	class tcp_proxy_socket;

	template <typename T>
	struct tcp_per_io_context : WSAOVERLAPPED
	{
		tcp_per_io_context(const proxy_io_operation io_operation, tcp_proxy_socket<T>* socket, const bool is_local)
			: WSAOVERLAPPED{0, 0, {{0, 0}}, nullptr},
			  io_operation(io_operation),
			  proxy_socket_ptr(socket),
			  is_local(is_local)
		{
		}

		proxy_io_operation io_operation;
		tcp_proxy_socket<T>* proxy_socket_ptr;
		WSABUF wsa_buf{0, nullptr};
		bool is_local;
	};

	template <typename T>
	class tcp_proxy_server;

	// ReSharper disable once CppClassCanBeFinal
	template <typename T>
	class tcp_proxy_socket
	{
		friend tcp_proxy_server<tcp_proxy_socket<T>>;

	public:
		using address_type_t = T;
		using negotiate_context_t = negotiate_context<T>;
		using per_io_context_t = tcp_per_io_context<T>;

	protected:
		constexpr static size_t send_receive_buffer_size = 256 * 256;

		/// <summary>local connection socket</summary>
		SOCKET local_socket_;
		/// <summary>remote connection socket</summary>
		SOCKET remote_socket_;

		std::unique_ptr<negotiate_context_t> negotiate_ctx_;

		/// <summary>message logging function</summary>
		std::function<void(const char*)> log_printer_;
		/// <summary>logging level</summary>
		netlib::log::log_level log_level_;

		bool is_disable_nagle_;

		/// <summary>provides synchronization for the I/O operations</summary>
		std::mutex lock_;
		connection_status connection_status_{connection_status::client_connected};

		std::array<char, send_receive_buffer_size> from_local_to_remote_buffer_{};
		std::array<char, send_receive_buffer_size> from_remote_to_local_buffer_{};

		WSABUF local_recv_buf_{
			static_cast<ULONG>(from_local_to_remote_buffer_.size()), from_local_to_remote_buffer_.data()
		};
		WSABUF local_send_buf_{0, nullptr};
		WSABUF remote_recv_buf_{
			static_cast<ULONG>(from_remote_to_local_buffer_.size()), from_remote_to_local_buffer_.data()
		};
		WSABUF remote_send_buf_{0, nullptr};

		per_io_context_t io_context_recv_from_local_{proxy_io_operation::relay_io_read, this, true};
		per_io_context_t io_context_recv_from_remote_{proxy_io_operation::relay_io_read, this, false};
		per_io_context_t io_context_send_to_local_{proxy_io_operation::relay_io_write, this, true};
		per_io_context_t io_context_send_to_remote_{proxy_io_operation::relay_io_write, this, false};

	public:
		tcp_proxy_socket(const SOCKET local_socket, const SOCKET remote_socket,
		                 std::unique_ptr<negotiate_context_t> negotiate_ctx,
		                 std::function<void(const char*)> log_printer, const netlib::log::log_level log_level,
		                 const bool disable_nagle = false)
			: local_socket_(local_socket),
			  remote_socket_(remote_socket),
			  negotiate_ctx_(std::move(negotiate_ctx)),
			  log_printer_(std::move(log_printer)),
			  log_level_(log_level),
			  is_disable_nagle_(disable_nagle)
		{
		}

		virtual ~tcp_proxy_socket()
		{
			std::lock_guard<std::mutex> lock(lock_);

			if (local_socket_ != INVALID_SOCKET)
			{
				shutdown(local_socket_, SD_BOTH);

				closesocket(local_socket_);

				local_socket_ = INVALID_SOCKET;
			}

			if (remote_socket_ != INVALID_SOCKET)
			{
				shutdown(remote_socket_, SD_BOTH);

				closesocket(remote_socket_);

				remote_socket_ = INVALID_SOCKET;
			}
		}

		tcp_proxy_socket(const tcp_proxy_socket& other) = delete;

		tcp_proxy_socket& operator=(const tcp_proxy_socket& other) = delete;

		// ReSharper disable once CppSpecialFunctionWithoutNoexceptSpecification
		tcp_proxy_socket(tcp_proxy_socket&& other)
		{
			std::unique_lock<std::mutex> th_lock(lock_, std::defer_lock);
			std::unique_lock<std::mutex> ot_lock(other.lock_, std::defer_lock);

			std::lock(th_lock, ot_lock);

			local_socket_ = other.local_socket_;
			other.local_socket_ = INVALID_SOCKET;
			remote_socket_ = other.remote_socket_;
			other.remote_socket_ = INVALID_SOCKET;
			negotiate_ctx_ = std::move(other.negotiate_ctx_);
			log_printer_ = std::move(other.log_printer_);
			log_level_ = other.log_level_;
			is_disable_nagle_ = other.is_disable_nagle_;
			connection_status_ = other.connection_status_;
			from_local_to_remote_buffer_ = std::move(other.from_local_to_remote_buffer_);
			from_remote_to_local_buffer_ = std::move(other.from_remote_to_local_buffer_);
			local_recv_buf_ = std::move(other.local_recv_buf_);
			local_send_buf_ = std::move(other.local_send_buf_);
			remote_recv_buf_ = std::move(other.remote_recv_buf_);
			remote_send_buf_ = std::move(other.remote_send_buf_);
			io_context_recv_from_local_ = std::move(other.io_context_recv_from_local_);
			io_context_recv_from_remote_ = std::move(other.io_context_recv_from_remote_);
			io_context_send_to_local_ = std::move(other.io_context_send_to_local_);
			io_context_send_to_remote_ = std::move(other.io_context_send_to_remote_);
		}

		// ReSharper disable once CppSpecialFunctionWithoutNoexceptSpecification
		tcp_proxy_socket& operator=(tcp_proxy_socket&& other)
		{
			using std::swap;
			swap(*this, other);
			return *this;
		}

		bool associate_to_completion_port(const ULONG_PTR completion_key, winsys::io_completion_port& completion_port)
		{
			connection_status_ = connection_status::client_established;

			if ((local_socket_ != INVALID_SOCKET) && (remote_socket_ != INVALID_SOCKET))
				return completion_port.associate_socket(local_socket_, completion_key) &&
					completion_port.associate_socket(remote_socket_, completion_key);
			return false;
		}

		template <bool IsLocked = true>
		void close_client(const bool is_receive, const bool is_local)
		{
			std::unique_lock<std::mutex> lock(lock_, std::defer_lock);

			if constexpr (!IsLocked)
			{
				lock.lock();
			}

			if (is_local)
			{
				if (local_socket_ != INVALID_SOCKET)
				{
					shutdown(local_socket_, SD_BOTH);
					closesocket(local_socket_);
					local_socket_ = INVALID_SOCKET;
					connection_status_ = connection_status::client_completed;
				}

				if (is_receive)
				{
					if (remote_send_buf_.len == 0)
					{
						if (remote_socket_ != INVALID_SOCKET)
						{
							shutdown(remote_socket_, SD_BOTH);
							closesocket(remote_socket_);
							remote_socket_ = INVALID_SOCKET;
							connection_status_ = connection_status::client_completed;
						}
					}

					local_recv_buf_.len = 0;
				}
				else
				{
					local_send_buf_.len = 0;
				}
			}
			else
			{
				if (remote_socket_ != INVALID_SOCKET)
				{
					shutdown(remote_socket_, SD_BOTH);
					closesocket(remote_socket_);
					remote_socket_ = INVALID_SOCKET;
					connection_status_ = connection_status::client_completed;
				}

				if (is_receive)
				{
					remote_recv_buf_.len = 0;

					if (local_send_buf_.len == 0)
					{
						if (local_socket_ != INVALID_SOCKET)
						{
							connection_status_ = connection_status::client_completed;
						}
					}
				}
				else
				{
					remote_send_buf_.len = 0;
				}
			}
		}

		bool is_ready_for_removal()
		{
			std::lock_guard<std::mutex> lock(lock_);

			if ((remote_socket_ == INVALID_SOCKET) &&
				(remote_send_buf_.len == 0) &&
				(local_send_buf_.len == 0) &&
				(remote_recv_buf_.len == 0))
			{
				if (local_socket_ != INVALID_SOCKET)
				{
					shutdown(local_socket_, SD_BOTH);
					closesocket(local_socket_);
					local_socket_ = INVALID_SOCKET;
				}

				if (local_recv_buf_.len == 0)
				{
					return true;
				}
			}

			return false;
		}

		// ********************************************************************************
		/// <summary>
		/// Attempts to negotiate credentials for local and remote sockets and starts 
		/// data relay between them
		/// </summary>
		/// <returns>true is relay was started, false otherwise</returns>
		// ********************************************************************************
		virtual bool start()
		{
			if (is_disable_nagle_)
			{
				auto i = 1;
				setsockopt(remote_socket_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&i), sizeof(i));
			}

			if (local_negotiate() && (remote_negotiate()))
			{
				// if negotiate phase can be complete immediately (or not needed at all)
				// start data relay here
				return start_data_relay();
			}
			// otherwise start_data_relay should be called from 
			// process_receive_negotiate_complete/process_send_negotiate_complete
			return false;
		}

		virtual void process_receive_negotiate_complete(const uint32_t io_size, per_io_context_t* io_context)
		{
		}

		virtual void process_send_negotiate_complete(const uint32_t io_size, per_io_context_t* io_context)
		{
		}

		virtual void process_receive_buffer_complete(const uint32_t io_size, per_io_context_t* io_context)
		{
			std::lock_guard<std::mutex> lock(lock_);

			switch (connection_status_)
			{
			case connection_status::client_completed:
				{
					if (io_context->is_local)
					{
						local_recv_buf_.len = 0;
					}
					else
					{
						remote_recv_buf_.len = 0;
					}

					break;
				}
			case connection_status::client_established:
				{
					if (io_context->is_local)
					{
						if (log_level_ > netlib::log::log_level::debug)
							log_printer(
								std::string(
									"process_receive_buffer_complete: data received from locally connected socket: ")
								+ std::to_string(io_size));

						// data received from locally connected socket
						if (remote_send_buf_.len == 0)
						{
							// if there is no "send to remotely connected socket" in progress
							// then forward the received data to remote host
							remote_send_buf_.buf = local_recv_buf_.buf;
							remote_send_buf_.len = io_size;

							if (log_level_ > netlib::log::log_level::debug)
								log_printer(
									std::string(
										"process_receive_buffer_complete: sending data to remotely connected socket: ")
									+ std::to_string(io_size));

							if ((::WSASend(
								remote_socket_,
								&remote_send_buf_,
								1,
								nullptr,
								0,
								&io_context_send_to_remote_,
								nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
							{
								// Close connection to remote peer in case of error
								close_client(false, false);
							}
						}

						// shift the receive buffer for the amount of received data
						// buffer is cyclic, adjust the available buffer size
						// if end of the buffer is reached then go from the start
						local_recv_buf_.buf += io_size;

						if (local_recv_buf_.buf > remote_send_buf_.buf)
						{
							if (local_recv_buf_.buf < from_local_to_remote_buffer_.data() + from_local_to_remote_buffer_
								.size())
							{
								local_recv_buf_.len = static_cast<ULONG>(from_local_to_remote_buffer_.data() +
									from_local_to_remote_buffer_.size() - local_recv_buf_.buf);
							}
							else
							{
								local_recv_buf_.buf = from_local_to_remote_buffer_.data();
								local_recv_buf_.len = static_cast<ULONG>(remote_send_buf_.buf -
									from_local_to_remote_buffer_.data());
							}
						}
						else
						{
							local_recv_buf_.len = static_cast<ULONG>(remote_send_buf_.buf - local_recv_buf_.buf);
						}

						// initiate the new receive if we have space in receive buffer
						if (local_recv_buf_.len)
						{
							DWORD flags = 0;

							if ((::WSARecv(
								local_socket_,
								&local_recv_buf_,
								1,
								nullptr,
								&flags,
								&io_context_recv_from_local_,
								nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
							{
								// Close connection to local peer in case of error
								close_client(true, true);
							}
						}
					}
					else
					{
						if (log_level_ > netlib::log::log_level::debug)
							log_printer(
								std::string(
									"process_receive_buffer_complete: data received from remotely connected socket: ")
								+ std::to_string(io_size));

						// data received from remotely connected socket
						if (local_send_buf_.len == 0)
						{
							// if there is no "send to locally connected socket" in progress
							// then forward the received data to local host
							local_send_buf_.buf = remote_recv_buf_.buf;
							local_send_buf_.len = io_size;

							if (log_level_ > netlib::log::log_level::debug)
								log_printer(
									std::string(
										"process_receive_buffer_complete: sending data to locally connected socket: ")
									+ std::to_string(io_size));

							if ((::WSASend(
								local_socket_,
								&local_send_buf_,
								1,
								nullptr,
								0,
								&io_context_send_to_local_,
								nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
							{
								// Close connection to local peer in case of error
								close_client(false, true);
							}
						}

						// shift the receive buffer for the amount of received data
						// buffer is cyclic, adjust the available buffer size
						// if end of the buffer is reached then go from the start
						remote_recv_buf_.buf += io_size;

						if (remote_recv_buf_.buf > local_send_buf_.buf)
						{
							if (remote_recv_buf_.buf < from_remote_to_local_buffer_.data() +
								from_remote_to_local_buffer_.size())
							{
								remote_recv_buf_.len = static_cast<DWORD>(from_remote_to_local_buffer_.data() +
									from_remote_to_local_buffer_.size() - remote_recv_buf_.buf
								);
							}
							else
							{
								remote_recv_buf_.buf = from_remote_to_local_buffer_.data();
								remote_recv_buf_.len = static_cast<DWORD>(local_send_buf_.buf -
									from_remote_to_local_buffer_.data());
							}
						}
						else
						{
							remote_recv_buf_.len = static_cast<DWORD>(local_send_buf_.buf - remote_recv_buf_.buf);
						}

						// initiate the new receive if we have space in receive buffer
						if (remote_recv_buf_.len)
						{
							DWORD flags = 0;

							if ((::WSARecv(
								remote_socket_,
								&remote_recv_buf_,
								1,
								nullptr,
								&flags,
								&io_context_recv_from_remote_,
								nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
							{
								// Close connection to remote peer in case of error
								close_client(true, false);
							}
						}
					}

					break;
				}
			case connection_status::client_no_change:
			case connection_status::client_connected:
				break;
			}
		}

		virtual void process_send_buffer_complete(const uint32_t io_size, per_io_context_t* io_context)
		{
			std::lock_guard<std::mutex> lock(lock_);

			if (io_context->is_local)
			{
				if (log_level_ > netlib::log::log_level::debug)
					log_printer(std::string("process_send_buffer_complete: send complete to locally connected socket: ")
						+ std::to_string(io_size));

				if (connection_status_ != connection_status::client_completed)
				{
					if (remote_recv_buf_.len == 0)
					{
						DWORD flags = 0;

						remote_recv_buf_.buf = local_send_buf_.buf;
						remote_recv_buf_.len = io_size;

						if (remote_recv_buf_.len > 0)
						{
							if ((::WSARecv(
								remote_socket_,
								&remote_recv_buf_,
								1,
								nullptr,
								&flags,
								&io_context_recv_from_remote_,
								nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
							{
								close_client(true, false);
							}
						}
					}
				}

				local_send_buf_.buf += io_size;

				if (local_send_buf_.buf == from_remote_to_local_buffer_.data() + from_remote_to_local_buffer_.size())
				{
					local_send_buf_.buf = from_remote_to_local_buffer_.data();
				}

				if (local_send_buf_.buf == remote_recv_buf_.buf)
				{
					if (connection_status_ == connection_status::client_completed)
					{
						close_client(false, false);
					}

					local_send_buf_.len = 0;
				}
				else
				{
					if (local_send_buf_.buf < remote_recv_buf_.buf)
					{
						local_send_buf_.len = static_cast<ULONG>(remote_recv_buf_.buf - local_send_buf_.buf);
					}
					else
					{
						local_send_buf_.len = static_cast<ULONG>(from_remote_to_local_buffer_.data() +
							from_remote_to_local_buffer_.size() - local_send_buf_.buf);
					}

					if (local_send_buf_.len)
					{
						if (log_level_ > netlib::log::log_level::debug)
							log_printer(
								std::string("process_send_buffer_complete: sending data to locally connected socket: ")
								+ std::to_string(io_size));

						if ((::WSASend(
							local_socket_,
							&local_send_buf_,
							1,
							nullptr,
							0,
							&io_context_send_to_local_,
							nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
						{
							close_client(false, true);
						}
					}
				}
			}
			else
			{
				if (log_level_ > netlib::log::log_level::debug)
					log_printer(
						std::string("process_send_buffer_complete: send complete to remotely connected socket: ")
						+ std::to_string(io_size));

				if (connection_status_ != connection_status::client_completed)
				{
					if (local_recv_buf_.len == 0)
					{
						DWORD flags = 0;

						local_recv_buf_.buf = remote_send_buf_.buf;
						local_recv_buf_.len = io_size;

						if (local_recv_buf_.len)
						{
							if ((::WSARecv(
								local_socket_,
								&local_recv_buf_,
								1,
								nullptr,
								&flags,
								&io_context_recv_from_local_,
								nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
							{
								close_client(true, true);
							}
						}
					}
				}

				remote_send_buf_.buf += io_size;

				if (remote_send_buf_.buf == from_local_to_remote_buffer_.data() + from_local_to_remote_buffer_.size())
				{
					remote_send_buf_.buf = from_local_to_remote_buffer_.data();
				}

				if (remote_send_buf_.buf == local_recv_buf_.buf)
				{
					if (connection_status_ == connection_status::client_completed)
					{
						close_client(false, false);
					}

					remote_send_buf_.len = 0;
				}
				else
				{
					if (remote_send_buf_.buf < local_recv_buf_.buf)
					{
						remote_send_buf_.len = static_cast<ULONG>(local_recv_buf_.buf - remote_send_buf_.buf);
					}
					else
					{
						remote_send_buf_.len = static_cast<ULONG>(from_local_to_remote_buffer_.data() +
							from_local_to_remote_buffer_.size() - remote_send_buf_.buf);
					}

					if (remote_send_buf_.len)
					{
						if (log_level_ > netlib::log::log_level::debug)
							log_printer(
								std::string("process_send_buffer_complete: sending data to remotely connected socket: ")
								+ std::to_string(io_size));

						if ((::WSASend(
							remote_socket_,
							&remote_send_buf_,
							1,
							nullptr,
							0,
							&io_context_send_to_remote_,
							nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
						{
							close_client(false, false);
						}
					}
				}
			}
		}

		static void process_inject_buffer_complete(per_io_context_t* context)
		{
			if (context->wsa_buf.buf != nullptr)
				delete[] context->wsa_buf.buf;

			delete context;
		}

		// ********************************************************************************
		/// <summary>
		/// Sends block of data into local socket
		/// </summary>
		/// <param name="data">data buffer</param>
		/// <param name="length">length of the data to send</param>
		/// <param name="type">type of operation</param>
		/// <returns>pre-status of the operation</returns>
		// ********************************************************************************
		bool inject_to_local(char* data, const uint32_t length,
		                     proxy_io_operation type = proxy_io_operation::inject_io_write)
		{
			auto context = new(std::nothrow) per_io_context_t{type, this, true};

			if (context == nullptr)
				return false;

			context->wsa_buf.buf = new(std::nothrow) char[length];

			if (context->wsa_buf.buf == nullptr)
			{
				delete context;
				return false;
			}

			memmove(context->wsa_buf.buf, data, length);

			context->wsa_buf.len = length;

			if ((::WSASend(
				local_socket_,
				&context->wsa_buf,
				1,
				nullptr,
				0,
				context,
				nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
			{
				close_client<false>(false, true);
				return false;
			}

			return true;
		}

		// ********************************************************************************
		/// <summary>
		/// Sends block of data into remote socket
		/// </summary>
		/// <param name="data">data buffer</param>
		/// <param name="length">length of the data to send</param>
		/// <param name="type">type of operation</param>
		/// <returns>pre-status of the operation</returns>
		// ********************************************************************************
		bool inject_to_remote(char* data, const uint32_t length,
		                      proxy_io_operation type = proxy_io_operation::inject_io_write)
		{
			auto context = new(std::nothrow) per_io_context_t{type, this, false};

			if (context == nullptr)
				return false;

			context->wsa_buf.buf = new(std::nothrow) char[length];

			if (context->wsa_buf.buf == nullptr)
			{
				delete context;
				return false;
			}

			memmove(context->wsa_buf.buf, data, length);

			context->wsa_buf.len = length;

			if ((::WSASend(
				remote_socket_,
				&context->wsa_buf,
				1,
				nullptr,
				0,
				context,
				nullptr) == SOCKET_ERROR) && (ERROR_IO_PENDING != WSAGetLastError()))
			{
				close_client<false>(false, false);
				return false;
			}

			return true;
		}

	protected:
		// ********************************************************************************
		/// <summary>
		/// Queries a pointer to the negotiate_context
		/// </summary>
		/// <returns> raw pointer to the negotiate_context</returns>
		// ********************************************************************************
		negotiate_context_t* get_negotiate_ctx() const
		{
			return negotiate_ctx_.get();
		}

		virtual bool local_negotiate()
		{
			return true;
		}

		virtual bool remote_negotiate()
		{
			return true;
		}

		bool start_data_relay()
		{
			DWORD flags = 0;

			auto ret = WSARecv(local_socket_, &local_recv_buf_, 1,
			                   nullptr, &flags, &io_context_recv_from_local_, nullptr);

			if (const auto wsa_error = WSAGetLastError(); ret == SOCKET_ERROR && (ERROR_IO_PENDING != wsa_error))
			{
				close_client<false>(true, true);

				remote_recv_buf_.len = 0;

				return false;
			}

			ret = WSARecv(remote_socket_, &remote_recv_buf_, 1,
			              nullptr, &flags, &io_context_recv_from_remote_, nullptr);

			if (const auto wsa_error = WSAGetLastError(); ret == SOCKET_ERROR && (ERROR_IO_PENDING != wsa_error))
			{
				closesocket(local_socket_);

				close_client<false>(true, false);

				return false;
			}

			return true;
		}

	private:
		void log_printer(const std::string& message) const
		{
			if (log_printer_)
			{
				log_printer_((std::string("tcp_proxy_socket: ") + message).c_str());
			}
		}
	};
}
