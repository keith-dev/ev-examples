#include <unistd.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <err.h>
#include <strings.h>

#include <sys/event.h>
#include <sys/aio.h>

#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <vector>
#include <map>
#include <memory>
#include <string>
#include <limits>

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>

//---------------------------------------------------------------------------
//
#define likely(x)	__builtin_expect(!!(x), 1) 
#define unlikely(x)	__builtin_expect(!!(x), 0) 

#define	FDBUF_SZ	(512 * 1024)

using fd_t = int;
using kq_t = int;
using kevents_t = std::vector<struct kevent>;

int trace(const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

//---------------------------------------------------------------------------
//
struct finfo_t
{
	finfo_t() = default;
	~finfo_t() = default;
	finfo_t(off_t offset, const char* name, u_short flags, u_int fflags, int64_t data);

	finfo_t(finfo_t&& n);
	finfo_t& operator=(finfo_t&& n) = delete;

	finfo_t(const finfo_t& n) = delete;
	finfo_t& operator=(const finfo_t& n) = delete;

	struct aiocb* fill_cb(fd_t fd, kq_t kq);

#ifndef NDEBUG
	void invariant() const;
#endif

	// file info
	off_t offset{-1};
	size_t namelen{std::numeric_limits<size_t>::max()};
	const char* name{nullptr};

	// kevent flags
	u_short flags{0};
	u_int fflags{0};
	int64_t data{0};

	// prebuilt comment
	int commentsz{-1};
	std::unique_ptr<char, decltype(&free)> comment{nullptr, free};

	// asynchronous i/o
#ifndef NDEBUG
	bool pending{false};
#endif
	struct aiocb cb;
	inline static constexpr size_t bufsz{FDBUF_SZ};
	std::unique_ptr<char, decltype(&free)> buf{nullptr, free};
};
using fileinfo_t = std::map<fd_t, finfo_t>;	// files by fd

#ifdef NDEBUG
inline
#endif
finfo_t::finfo_t(off_t offset, const char* name, u_short flags, u_int fflags, int64_t data) :
	// file info
	offset(offset), namelen(strlen(name)), name(name),

	// kevent options
	flags(flags), fflags(fflags), data(data),

	// asynchronous i/o
#ifndef NDEBUG
	pending(false),
#endif
	buf((char*)malloc(bufsz), free)
{
	if (!buf.get())
		err(EXIT_FAILURE, "cannot create finfo_t::buf");

	char* p = nullptr;
	commentsz = asprintf(&p, "\n==== %s ====\n\n", name);
	if (!p)
		err(EXIT_FAILURE, "cannot create finfo_t(%s)", name);
	comment.reset(p);
#ifndef NDEBUG
	invariant();
#endif
}

#ifdef NDEBUG
inline
#endif
finfo_t::finfo_t(finfo_t&& n) :
	// file info
	offset(n.offset), namelen(n.namelen), name(n.name),

	// kevent options
	flags(n.flags), fflags(n.fflags), data(n.data),

	// prebuilt comment
	commentsz(n.commentsz), comment(std::move(n.comment)),

	// asynchronous i/o
#ifndef NDEBUG
	pending(n.pending),
#endif
	cb(n.cb), buf(std::move(n.buf))
{
	n.offset	= 0;
	n.namelen	= 0;
	n.name		= nullptr;

	n.flags		= 0;
	n.fflags	= 0;
	n.data		= 0;

	n.commentsz	= 0;
#ifndef NDEBUG
	n.pending	= false;
	invariant();
#endif
}

#ifndef NDEBUG
void finfo_t::invariant() const
{
	assert((name == nullptr && namelen == std::numeric_limits<size_t>::max()) || strlen(name) == namelen);
	assert((comment.get() == nullptr && commentsz == -1) || strlen(comment.get()) == static_cast<size_t>(commentsz));
	assert(buf.get() && bufsz == FDBUF_SZ);
}
#endif

#ifdef NDEBUG
inline
#endif
struct aiocb* finfo_t::fill_cb(fd_t fd, kq_t kq)
{
#ifndef NDEBUG
	invariant();
#endif
	bzero(&cb, sizeof(cb));
	cb.aio_fildes = fd;
	cb.aio_offset = offset;
	cb.aio_buf    = buf.get();
	cb.aio_nbytes = bufsz;
	cb.aio_sigevent.sigev_notify_kqueue = kq;
	cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
	cb.aio_sigevent.sigev_value.sigval_int = fd;

	return &cb;
}

//---------------------------------------------------------------------------
//
fd_t create_tcp4_server(const char* host, uint16_t port) {
	in_addr addr_in;
	int ret = inet_aton(host, &addr_in);
	if (ret == -1)
		return -1;

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr = addr_in;
	addr.sin_port = htons(port);

	fd_t s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == -1)
		return -1;

	ret = bind(s, (sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		close(s);
		return -1;
	}

	ret = listen(s, 128);
	return s;
}

fd_t create_udp4_server(const char* host, uint16_t port) {
	in_addr addr_in;
	int ret = inet_aton(host, &addr_in);
	if (ret == -1)
		return -1;

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr = addr_in;
	addr.sin_port = htons(port);

	fd_t s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1)
		return -1;

	ret = bind(s, (sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		close(s);
		return -1;
	}

	return s;
}

std::tuple<fd_t, u_short, u_int, int64_t> factory(const char* name) {
	if (strlen(name) > 4) {
		if (const char* port = strrchr(name + 4, ':')) {
			uint16_t nport = atoi(port + 1);
			std::string addr(name + 4, port - name - 4);

			if (strncmp(name, "udp:", 4) == 0)
				return std::make_tuple(create_udp4_server(addr.c_str(), nport),
										EVFILT_READ,
										EV_ADD | EV_CLEAR,
										0);
			if (strncmp(name, "tcp:", 4) == 0)
				return std::make_tuple(create_tcp4_server(addr.c_str(), nport),
										EVFILT_READ,
										EV_ADD | EV_CLEAR,
										0);
			return std::make_tuple(-1, 0, 0, 0);
		}
	}

	return std::make_tuple(open(name, O_RDONLY),
							EVFILT_VNODE,
							EV_ADD | EV_CLEAR,
							NOTE_WRITE);
}

fileinfo_t make_fileinfo(int argc, char* argv[])
{
	fileinfo_t files;

	for (int i = 1; i < argc; ++i) {
		auto [fd, flags, fflags, data] = factory(argv[i]);
		if (fd == -1)
			err(EXIT_FAILURE, "cannot open: %s", argv[i]);

		off_t offset{};
		if (data) {
			offset = lseek(fd, 0, SEEK_END);
			if (offset == -1)
				offset = 0;
		}
		files.emplace(fd, finfo_t(offset, argv[i], flags, fflags, data));
	}

	return files;
}

kevents_t make_events(fileinfo_t& files)
{
	std::vector<struct kevent> events{files.size()};

	size_t i{};
	fileinfo_t::iterator p{ files.begin() };

	for (; p != files.end(); ++p, ++i) {
		fd_t fd = p->first;
		EV_SET(&events[i], fd, p->second.flags, p->second.fflags, p->second.data, 0, NULL);
	}

	return events;
}

ssize_t write(fd_t fd, finfo_t& file, const char* buf, size_t nbytes)
{
	static fd_t lastfd{-1};

	ssize_t sent{};

	if (nbytes > 0) {
		if (lastfd != -1 && fd != lastfd)
			write(fd, file.comment.get(), file.commentsz);
		
		while (static_cast<size_t>(sent) < nbytes) {
			ssize_t ret = write(fd, buf, nbytes - sent);
			if (unlikely(ret == -1))
				err(EXIT_FAILURE, "write(%d, buf, %ld) failed", fd, nbytes - sent);

			sent += ret;
		}

		file.offset += nbytes;
	}

	lastfd = fd;
	return sent;
}

//---------------------------------------------------------------------------
//
int trace(const char* fmt, ...)
{
	if (getenv("NOTRACE"))
		return 0;

	va_list args;
	va_start(args, fmt);

	ssize_t nbytes1 = write(STDERR_FILENO, "trace: ", 7);
	ssize_t nbytes2 = 0;

	char* buf = nullptr;
	ssize_t nbytes = vasprintf(&buf, fmt, args);
	if (nbytes != -1) {
		std::unique_ptr<char, decltype(&free)> tmp(buf, free);

		if (likely(nbytes > 0))
			nbytes2 += write(STDERR_FILENO, buf, nbytes);
	}

	va_end(args);
	return int(nbytes1 + nbytes2);
}

std::string flags_str(u_short flags)
{
	std::string str;

	for (decltype(flags) i = 1; i != 0; i <<= 1) {
		if ((flags & i) == EV_ADD)		str += " | EV_ADD";
		if ((flags & i) == EV_ENABLE)	str += " | EV_ENABLE";
		if ((flags & i) == EV_DISABLE)	str += " | EV_DISABLE";
		if ((flags & i) == EV_DISPATCH)	str += " | EV_DISPATCH";
		if ((flags & i) == EV_DELETE)	str += " | EV_DELETE";
		if ((flags & i) == EV_RECEIPT)	str += " | EV_RECEIPT";
		if ((flags & i) == EV_ONESHOT)	str += " | EV_ONESHOT";
		if ((flags & i) == EV_CLEAR)	str += " | EV_CLEAR";
		if ((flags & i) == EV_EOF)		str += " | EV_EOF";
	}

	return (str.size() > 3) ? str.substr(3) : std::string();
}

std::string fflags_str(u_int fflags)
{
	std::string str;

	for (decltype(fflags) i = 1; i != 0; i <<= 1) {
		if ((fflags & i) == NOTE_DELETE)		str += " | NOTE_DELETE";
		if ((fflags & i) == NOTE_WRITE)			str += " | NOTE_WRITE";
		if ((fflags & i) == NOTE_EXTEND)		str += " | NOTE_EXTEND";
		if ((fflags & i) == NOTE_ATTRIB)		str += " | NOTE_ATTRIB";
		if ((fflags & i) == NOTE_LINK)			str += " | NOTE_LINK";
		if ((fflags & i) == NOTE_RENAME)		str += " | NOTE_RENAME";
		if ((fflags & i) == NOTE_REVOKE)		str += " | NOTE_REVOKE";
		if ((fflags & i) == NOTE_OPEN)			str += " | NOTE_OPEN";
		if ((fflags & i) == NOTE_CLOSE)			str += " | NOTE_CLOSE";
		if ((fflags & i) == NOTE_CLOSE_WRITE)	str += " | NOTE_CLOSE_WRITE";
		if ((fflags & i) == NOTE_READ)			str += " | NOTE_READ";
	}

	return (str.size() > 3) ? str.substr(3) : std::string();
}

//------------------------------------------------------------/---------------
//
void on_read(finfo_t& file, const char* buf, int nbytes) {
	write(STDOUT_FILENO, file, buf, nbytes);
}

void decode_events(fileinfo_t& files, kq_t kq, int i, const struct kevent& tevent)
{
	trace("event[%d]: ident:0x%lx flags:0x%hx (%s) fflags:0x%x (%s) data:0x%lx udata:%p\n",
		i, tevent.ident,
		tevent.flags, flags_str(tevent.flags).c_str(),
		tevent.fflags, fflags_str(tevent.fflags).c_str(),
		tevent.data, tevent.udata);
	if (tevent.flags & EV_ERROR) {
		trace("ERROR\n");
		err(EXIT_FAILURE, "ERROR: code=%d error=\"%s\"", errno, strerror(errno));
	}

	// complete asynchronous read
	if (tevent.udata) {
		fd_t fd{ static_cast<fd_t>( reinterpret_cast<long>(tevent.udata) ) };
		finfo_t& file = files.find(fd)->second;
		struct aiocb* cb = reinterpret_cast<struct aiocb*>(tevent.ident);

#ifndef NDEBUG
		if (!file.pending)
			trace("unexpected aio complete\n");
		file.pending = false;
#endif

		int nbytes = aio_return(cb);
		if (nbytes == -1) {
			trace("async read failed code=%d error=\"%s\"\n", errno, strerror(errno));
			return;
		}

		const char* buf{ static_cast<char*>( const_cast<void*>(file.cb.aio_buf) ) };
		trace("aio_return: offset=%ld nbytes=%d\n", file.offset, nbytes); 
//		write(STDOUT_FILENO, file, buf, nbytes);
		on_read(file, buf, nbytes);
		return;
	}

	// initiate asynchronous read
	fd_t fd{ static_cast<fd_t>(tevent.ident) };
	fileinfo_t::iterator pfile = files.find(fd);
	if (pfile == files.end()) {
		trace("ERROR: fd lookup\n");
		err(EXIT_FAILURE, "ERROR: fd=%d lookup", fd);
	}
	finfo_t& file = pfile->second;

#ifndef NDEBUG
	if (file.pending) {
		trace("aio_read pending: dropping aio_read request\n");
		return;
	}
	file.pending = true;
#endif

	// add aio_read
	int ret{ aio_read(file.fill_cb(fd, kq)) };
	trace("aio_read(fd=%d offset=%ld nbytes=%ld)=%d code=%d error=\"%s\"\n",
		fd, file.offset, file.bufsz, ret, errno, strerror(errno));
	if (ret == 0)
		return; // async read request accepted

	// async read request failed
	ret = aio_cancel(fd, &file.cb);
	trace("aio_cancel() code=%d text=%s\n", ret,
		(ret == AIO_CANCELED    ? "AIO_CANCELED" :
		 ret == AIO_NOTCANCELED ? "AIO_NOTCANCELED" :
		 ret == AIO_ALLDONE     ? "AIO_ALLDONE" : strerror(errno)));
#ifndef NDEBUG
	file.pending = false;
#endif
	if (ret == AIO_ALLDONE) {
		trace("asynchronous read request has completed\n");
		return;
	}

	// fall back to synchronous read
	trace("falling back to synchronous read\n");
	char* buf{ static_cast<char*>(const_cast<void*>(file.cb.aio_buf)) };
	size_t bufsz{ file.bufsz };

//	assert(file.offset == lseek(fd, 0, SEEK_CUR));
	int nbytes = read(fd, buf, bufsz);
	if (nbytes == -1)
		err(EXIT_FAILURE, "read failed code=%d error=\"%s\"", errno, strerror(errno));
	if (nbytes == 0)
		return;
	trace("read: nbytes=%d\n", nbytes); 
//	write(STDOUT_FILENO, file, buf, nbytes);
	on_read(file, buf, nbytes);
}

namespace
{
	bool s_stop = false;

	void handle_signal(int)
	{
		s_stop = true;
	}
}

int main(int argc, char* argv[])
{
	struct sigaction sa;
	sa.sa_handler = &handle_signal;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);

	fileinfo_t files{ make_fileinfo(argc, argv) };
	if (files.empty())
		return 0;

	kevents_t events{ make_events(files) };
	kevents_t tevents{ 2 * files.size()} ; // allow error and fired on each

	kq_t kq = kqueue();
	if (kq == -1)
		err(EXIT_FAILURE, "kqueue(): code=%d error%s", errno, strerror(errno));

	for (; !s_stop; ) {
		// wait for an event
		int n_tevents{ kevent(
				kq,
				!events.empty() ? &events.front() : NULL, events.size(),
				&tevents.front(), tevents.size(), NULL) };
		trace("kevent(kq, events=%lu, tevents=%lu, NULL)=%d\n", events.size(), tevents.size(), n_tevents);
		if (n_tevents == -1)
			err(EXIT_FAILURE, "kevent failed code=%d error=\"%s\"", errno, strerror(errno));

		for (int i = 0; i != n_tevents; ++i)
			decode_events(files, kq, i, tevents[i]);
	}
}
