// based on: https://gist.github.com/koblas/3364414

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <resolv.h>

#include <ev++.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <list>
#include <memory>
#include <utility>

//
//	Buffer class:	allow for output buffering such that it can be written out
//					into async pieces
//
class Buffer {
	std::unique_ptr<char> data_;
	ssize_t len_{};
	ssize_t pos_{};

public:
	Buffer() = default;
	~Buffer() = default;

	Buffer(const Buffer &) = delete;
	Buffer& operator=(const Buffer &) = delete;

	Buffer(Buffer &) = delete;
	Buffer& operator=(Buffer &) = delete;

	Buffer(std::unique_ptr<char> data, ssize_t nbytes) :
		data_(std::move(data)), len_(nbytes) {
	}

	char *dpos() {
		return data_.get() + pos_;
	}

	void bump(ssize_t nbytes) {
		pos_ = std::min(len_, pos_ + nbytes); 
	}

	ssize_t nbytes() {
		return len_ - pos_;
	}
};

//
//   A single instance of a non-blocking Echo handler
//
class EchoInstance {
	static int total_clients;
	ev::io	io_;
	int	    sfd_;

	// Buffers that are pending write
	// need smart pointer because of how we terminate
	std::list<std::unique_ptr<Buffer>> write_queue_;

	// Generic callback
	void callback(ev::io &watcher, int revents) {
		if (EV_ERROR & revents) {
			perror("got invalid event");
			return;
		}

		if (revents & EV_READ)
			read_cb(watcher);

		if (revents & EV_WRITE)
			write_cb(watcher);

		if (write_queue_.empty()) {
			io_.set(ev::READ);
		} else {
			io_.set(ev::READ|ev::WRITE);
		}
	}

	// Socket is writable
	void write_cb(ev::io &watcher) {
		if (write_queue_.empty()) {
			io_.set(ev::READ);
			return;
		}

		Buffer* buffer = write_queue_.front().get();

		ssize_t written = write(watcher.fd, buffer->dpos(), buffer->nbytes());
		if (written == -1) {
			perror("read error");
			return;
		}

		buffer->bump(written);
		if (buffer->nbytes() == 0)
			write_queue_.pop_front();
	}

	// Receive message from client socket
	void read_cb(ev::io &watcher) {
		constexpr size_t buffersz{4*1024};
		std::unique_ptr<char> buffer{std::make_unique<char>(buffersz)};

		ssize_t nread = recv(watcher.fd, buffer.get(), buffersz, 0);
		if (nread == -1) {
			perror("read error");
			return;
		}

		if (nread == 0) {
			// Gack - we're deleting ourself inside of ourself!
			delete this;
			return;
		}

		// Send message bach to the client
		write_queue_.push_back(std::make_unique<Buffer>(std::move(buffer), nread));
	}

	// private because we kill ourselves
	~EchoInstance() {
		// Stop and free watcher if client socket is closing
		io_.stop();

		close(sfd_);

		printf("%d client(s) connected.\n", --total_clients);
	}

public:
	EchoInstance(int s) : sfd_(s) {
		fcntl(sfd_, F_SETFL, fcntl(sfd_, F_GETFL, 0) | O_NONBLOCK);

		printf("Got connection: %d\n", ++total_clients);

		io_.set<EchoInstance, &EchoInstance::callback>(this);

		io_.start(sfd_, ev::READ);
	}
};

class EchoServer {
	ev::io	 io_;
	ev::sig	 sio_;
	int		 s_{-1};

public:
	void io_accept(ev::io& watcher, int revents) {
		if (EV_ERROR & revents) {
			perror("got invalid event");
			return;
		}

		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);

		int client_sd = accept(watcher.fd, (struct sockaddr*)&client_addr, &client_len);
		if (client_sd == -1) {
			perror("accept error");
			return;
		}

		// Let them float free and clean up themselves
		new EchoInstance(client_sd);
	}

	static void signal_cb(ev::sig &signal, int revents) {
		signal.loop.break_loop();
	}

	EchoServer(uint16_t port) {
		printf("Listening on port %d\n", port);

		s_ = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);

		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port   = htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;

		if (bind(s_, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
			perror("bind");
		}

		fcntl(s_, F_SETFL, fcntl(s_, F_GETFL, 0) | O_NONBLOCK);

		struct linger sl;
		sl.l_onoff = 1;         /* non-zero value enables linger option in kernel */
		sl.l_linger = 0;        /* timeout interval in seconds */
		setsockopt(s_, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));

		listen(s_, 5);

		io_.set<EchoServer, &EchoServer::io_accept>(this);
		io_.start(s_, ev::READ);

		sio_.set<&EchoServer::signal_cb>();
		sio_.start(SIGINT);
	}

	~EchoServer() {
		// stop accepting connections
		shutdown(s_, SHUT_RD);

		// we're done, don't care about existing connections
		close(s_);
	}
};

int EchoInstance::total_clients = 0;

int main(int argc, char **argv)
{
	uint16_t port = 2222;
	if (argc > 1)
		port = atoi(argv[1]);

	ev::default_loop loop;
	EchoServer echo(port);

	loop.run(0);
}
