//	started with: https://gist.github.com/koblas/3364414

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

//
//	Buffer class:	allow for output buffering such that it can be written out
//					into async pieces
//
class Buffer {
	char*   data;
	ssize_t len;
	ssize_t pos;

public:
	Buffer(const char* bytes, ssize_t nbytes) :
		data(new char[nbytes]), len(nbytes), pos(0) {
		memcpy(data, bytes, nbytes);
	}

	~Buffer() {
		delete [] data;
	}

	Buffer(const Buffer &) = delete;
	Buffer& operator=(const Buffer &) = delete;

	Buffer(Buffer &) = delete;
	Buffer& operator=(Buffer &) = delete;

	char *dpos() {
		return data + pos;
	}

	void bump(ssize_t nbytes) {
		pos += nbytes;
	}

	ssize_t nbytes() {
		return len - pos;
	}
};

//
//   A single instance of a non-blocking Echo handler
//
class EchoInstance {
private:
	static int total_clients;
	ev::io	io;
	int	    sfd;

	// Buffers that are pending write
	// need smart pointer because of how we terminate
	std::list<std::unique_ptr<Buffer>> write_queue;

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

		if (write_queue.empty()) {
			io.set(ev::READ);
		} else {
			io.set(ev::READ|ev::WRITE);
		}
	}

	// Socket is writable
	void write_cb(ev::io &watcher) {
		if (write_queue.empty()) {
			io.set(ev::READ);
			return;
		}

		Buffer* buffer = write_queue.front().get();

		ssize_t written = write(watcher.fd, buffer->dpos(), buffer->nbytes());
		if (written == -1) {
			perror("read error");
			return;
		}

		buffer->bump(written);
		if (buffer->nbytes() == 0)
			write_queue.pop_front();
	}

	// Receive message from client socket
	void read_cb(ev::io &watcher) {
		char buffer[4*1024];

		ssize_t nread = recv(watcher.fd, buffer, sizeof(buffer), 0);
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
		write_queue.push_back(std::make_unique<Buffer>(buffer, nread));
	}

	// effictivly a close and a destroy
	~EchoInstance() {
		// Stop and free watcher if client socket is closing
		io.stop();

		close(sfd);

		printf("%d client(s) connected.\n", --total_clients);
	}

public:
	EchoInstance(int s) : sfd(s) {
		fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

		printf("Got connection\n");
		total_clients++;

		io.set<EchoInstance, &EchoInstance::callback>(this);

		io.start(s, ev::READ);
	}
};

class EchoServer {
private:
	ev::io	 io;
	ev::sig	 sio;
	int		 s;

public:
	void io_accept(ev::io &watcher, int revents) {
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

		// Let them float free and clean up themselves?
		EchoInstance* client = new EchoInstance(client_sd);
	}

	static void signal_cb(ev::sig &signal, int revents) {
		signal.loop.break_loop();
	}

	EchoServer(uint16_t port) {
		printf("Listening on port %d\n", port);

		s = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);

		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port   = htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;

		if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
			perror("bind");
		}

		fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

		listen(s, 5);

		io.set<EchoServer, &EchoServer::io_accept>(this);
		io.start(s, ev::READ);

		sio.set<&EchoServer::signal_cb>();
		sio.start(SIGINT);
	}

	~EchoServer() {
		// stop accepting connections
		shutdown(s, SHUT_RD);

		// service existing connections
		sleep(1);

		// we're done
		close(s);
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
