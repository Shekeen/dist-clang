#include <net/connection_impl.h>

#include <base/assert.h>
#include <base/empty_lambda.h>
#include <base/logging.h>
#include <net/event_loop.h>

#include <sys/socket.h>

#include <base/using_log.h>

using namespace std::placeholders;

namespace dist_clang {
namespace net {

// static
ConnectionImplPtr ConnectionImpl::Create(EventLoop& event_loop, Socket&& fd,
                                         const EndPointPtr& end_point) {
  DCHECK(fd.IsBlocking());
  return ConnectionImplPtr(
      new ConnectionImpl(event_loop, std::move(fd), end_point));
}

ConnectionImpl::ConnectionImpl(EventLoop& event_loop, Socket&& fd,
                               const EndPointPtr& end_point)
    : fd_(std::move(fd)),
      event_loop_(event_loop),
      is_closed_(false),
      added_(false),
      end_point_(end_point),
      file_input_stream_(fd_.native(), buffer_size),
      gzip_input_stream_(
          new GzipInputStream(&file_input_stream_, GzipInputStream::ZLIB)),
      file_output_stream_(fd_.native(), buffer_size),
      counter_("Connection"_l, perf::LogReporter::HUMAN) {}

ConnectionImpl::~ConnectionImpl() {
  Close();
}

bool ConnectionImpl::ReadAsync(ReadCallback callback) {
  auto shared = std::static_pointer_cast<ConnectionImpl>(shared_from_this());
  read_callback_ = std::bind(callback, shared_from_this(), _1, _2);
  if (!event_loop_.ReadyForRead(shared)) {
    read_callback_ = BindedReadCallback();
    return false;
  }
  return true;
}

bool ConnectionImpl::ReadSync(Message* message, Status* status) {
  if (!message) {
    if (status) {
      status->set_code(Status::EMPTY_MESSAGE);
      status->set_description("Message pointer is null");
    }
    return false;
  }

  if (is_closed_) {
    if (status) {
      status->set_code(Status::INCONSEQUENT);
      status->set_description("Reading after close");
    }
    return false;
  }

  ui32 size;
  {
    CodedInputStream coded_stream(gzip_input_stream_.get());
    if (!coded_stream.ReadVarint32(&size)) {
      if (status) {
        status->set_code(Status::NETWORK);

        if (file_input_stream_.ByteCount() == 0 &&
            (file_input_stream_.GetErrno() == EAGAIN ||
             file_input_stream_.GetErrno() == EWOULDBLOCK)) {
          status->set_description("Read operation timeout");
        } else {
          status->set_description(
              "Can't read incoming message size (read " +
              std::to_string(file_input_stream_.ByteCount()) + " raw bytes)");
          if (file_input_stream_.GetErrno()) {
            status->mutable_description()->append(": ");
            status->mutable_description()->append(
                strerror(file_input_stream_.GetErrno()));
          } else if (gzip_input_stream_->ZlibErrorMessage()) {
            status->mutable_description()->append(": ");
            status->mutable_description()->append(
                gzip_input_stream_->ZlibErrorMessage());
          }
        }
      }
      return false;
    }
  }

  if (!size) {
    if (status) {
      status->set_code(Status::BAD_MESSAGE);
      status->set_description("Incoming message has zero size");
    }
    return false;
  }

  if (!message->ParseFromBoundedZeroCopyStream(gzip_input_stream_.get(),
                                               size)) {
    if (status) {
      status->set_code(Status::BAD_MESSAGE);
      status->set_description("Incoming message is malformed");
      if (file_input_stream_.GetErrno()) {
        status->mutable_description()->append(": ");
        status->mutable_description()->append(
            strerror(file_input_stream_.GetErrno()));
      } else if (gzip_input_stream_->ZlibErrorMessage()) {
        status->mutable_description()->append(": ");
        status->mutable_description()->append(
            gzip_input_stream_->ZlibErrorMessage());
      }
    }
    return false;
  }

  return true;
}

bool ConnectionImpl::SendTimeout(ui32 sec_timeout, String* error) {
  if (!fd_.SendTimeout(sec_timeout, error)) {
#if defined(OS_MACOSX)
    // According to Mac OS X manual the |setsockopt()| may return EINVAL in case
    // the socket is already shut down.
    if (errno == EINVAL) {
      DCHECK(IsClosed());
    }
#endif  // defined(OS_MACOSX)
    return false;
  }
  return true;
}

bool ConnectionImpl::ReadTimeout(ui32 sec_timeout, String* error) {
  if (!fd_.ReadTimeout(sec_timeout, error)) {
#if defined(OS_MACOSX)
    // According to Mac OS X manual the |setsockopt()| may return EINVAL in case
    // the socket is already shut down.
    if (errno == EINVAL) {
      DCHECK(IsClosed());
    }
#endif  // defined(OS_MACOSX)
    return false;
  }
  return true;
}

bool ConnectionImpl::SendAsyncImpl(SendCallback callback) {
  auto shared = std::static_pointer_cast<ConnectionImpl>(shared_from_this());
  send_callback_ = std::bind(callback, shared_from_this(), _1);
  if (!event_loop_.ReadyForSend(shared)) {
    send_callback_ = EmptyLambda<bool>(false);
    return false;
  }
  return true;
}

bool ConnectionImpl::SendSyncImpl(Status* status) {
  if (is_closed_) {
    if (status) {
      status->set_code(Status::INCONSEQUENT);
      status->set_description("Reading after close");
    }
    return false;
  }

  if (!gzip_output_stream_) {
    GzipOutputStream::Options options;
    options.format = GzipOutputStream::ZLIB;
    gzip_output_stream_.reset(
        new GzipOutputStream(&file_output_stream_, options));
  }

  {
    CodedOutputStream coded_stream(gzip_output_stream_.get());
    coded_stream.WriteVarint32(message_->ByteSize());
  }

  if (!message_->SerializeToZeroCopyStream(gzip_output_stream_.get())) {
    if (status) {
      status->set_code(Status::NETWORK);
      status->set_description("Can't serialize message to GZip stream");
      if (file_output_stream_.GetErrno()) {
        status->mutable_description()->append(": ");
        status->mutable_description()->append(
            strerror(file_output_stream_.GetErrno()));
      } else if (gzip_output_stream_->ZlibErrorMessage()) {
        auto* description = status->mutable_description();
        description->append(": ");
        description->append(gzip_output_stream_->ZlibErrorMessage());
      }
    }
    return false;
  }

  if (!gzip_output_stream_->Flush()) {
    if (status) {
      status->set_code(Status::NETWORK);
      status->set_description("Can't flush gzipped message");
      if (gzip_output_stream_->ZlibErrorMessage()) {
        auto* description = status->mutable_description();
        description->append(": ");
        description->append(gzip_output_stream_->ZlibErrorMessage());
      }
    }
    return false;
  }

  // The method |Flush()| calls function |write()| and potentially can raise
  // the signal |SIGPIPE|.
  if (!file_output_stream_.Flush()) {
    if (status) {
      status->set_code(Status::NETWORK);
      status->set_description("Can't flush sent message to socket");
      if (file_output_stream_.GetErrno()) {
        status->mutable_description()->append(": ");
        status->mutable_description()->append(
            strerror(file_output_stream_.GetErrno()));
      }
    }
    return false;
  }

  return true;
}

void ConnectionImpl::DoRead() {
  Status status;
  message_.reset(new Message);
  auto result = ReadSync(message_.get(), &status);
  DCHECK(!!read_callback_);
  auto read_callback = read_callback_;
  read_callback_ = BindedReadCallback();
  if (!read_callback(std::move(message_), status) || !result) {
    Close();
  }
}

void ConnectionImpl::DoSend() {
  Status status;
  auto result = Connection::SendSync(std::move(message_), &status);
  DCHECK(!!send_callback_);
  auto send_callback = send_callback_;
  send_callback_ = EmptyLambda<bool>(false);
  if (!send_callback(status) || !result) {
    Close();
  }
}

void ConnectionImpl::Close() {
  bool old_closed = false;
  if (is_closed_.compare_exchange_strong(old_closed, true)) {
    read_callback_ = EmptyLambda<bool>(false);
    send_callback_ = EmptyLambda<bool>(false);
    gzip_output_stream_.reset();
    gzip_input_stream_.reset();
    file_output_stream_.Flush();
    shutdown(fd_.native(), SHUT_RDWR);
    char discard[buffer_size];
    while (read(fd_.native(), discard, buffer_size) > 0) {
    }
  }
}

}  // namespace net
}  // namespace dist_clang
