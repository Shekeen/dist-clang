#include <net/connection.h>

#include <base/logging.h>

#include <base/using_log.h>

namespace dist_clang {
namespace net {

template <>
bool Connection::SendAsync(ScopedMessage message, SendCallback callback) {
  message_ = std::move(message);
  return SendAsyncImpl(callback);
}

template <>
bool Connection::SendSync(ScopedMessage message, Status* status) {
  message_ = std::move(message);
  return SendSyncImpl(status);
}

// static
Connection::SendCallback Connection::CloseAfterSend() {
  return [](ConnectionPtr, const Status& status) {
    if (status.code() != Status::OK) {
      LOG(ERROR) << "Failed to send message: " << status.description();
    }
    return false;
  };
}

bool Connection::ReportStatus(const Status& message, SendCallback callback) {
  message_.reset(new Message);
  message_->SetAllocatedExtension(Status::extension, new Status(message));
  return SendAsyncImpl(callback);
}

}  // namespace net
}  // namespace dist_clang
