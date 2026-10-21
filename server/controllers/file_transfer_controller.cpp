#include "file_transfer_controller.hpp"
#include "console_lease.hpp"
#include <sys/socket.h>
#include <cstdlib>
#include <string>

namespace controllers {

bool FileTransferController::SendAll(int fd, const char* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    int r = ::send(fd, data + sent, len - sent, 0);
    if (r <= 0) return false;
    sent += r;
  }
  return true;
}

std::string FileTransferController::UrlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      char hex[3] = {s[i + 1], s[i + 2], '\0'};
      char* end;
      long val = strtol(hex, &end, 16);
      if (end == hex + 2) {
        out += static_cast<char>(val);
        i += 2;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

std::string FileTransferController::QueryParam(const std::string& qs,
                                               const std::string& name) {
  const std::string key = name + "=";
  size_t pos = 0;
  while ((pos = qs.find(key, pos)) != std::string::npos) {
    if (pos == 0 || qs[pos - 1] == '&') {
      pos += key.size();
      auto end = qs.find('&', pos);
      return UrlDecode(qs.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
    }
    pos += key.size();
  }
  return {};
}

std::string FileTransferController::ParsePathParam(const std::string& qs) {
  return QueryParam(qs, "path");
}

http::Response FileTransferController::Handle(const http::Request& req) {
  if (req.path == "/fs/download") {
    if (req.method != "GET") return http::Response::MethodNotAllowed();
    std::string path = ParsePathParam(req.query_string);
    if (path.empty()) return http::Response::BadRequest("missing path parameter");

    int fd = req.fd;
    auto status = service_.Download(path,
      [fd](size_t size) {
        std::string h = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                        "Content-Length: " + std::to_string(size) +
                        "\r\nConnection: close\r\n\r\n";
        SendAll(fd, h.data(), h.size());
      },
      [fd](const char* data, size_t n) -> bool {
        return SendAll(fd, data, n);
      });

    if (status == services::TransferStatus::NotFound)
      return http::Response::NotFound();
    if (status != services::TransferStatus::Ok)
      return {500, "text/plain", "internal error"};
    return http::Response::Raw();
  }

  if (req.path == "/fs/upload") {
    if (req.method != "PUT") return http::Response::MethodNotAllowed();
    std::string path = ParsePathParam(req.query_string);
    if (path.empty()) return http::Response::BadRequest("missing path parameter");
    // Uploads change the SD card, so they follow the same console lease as
    // the write tools: ?agent=<session name>.
    std::string refused =
        services::ConsoleLease::Instance().Use(QueryParam(req.query_string, "agent"));
    if (!refused.empty()) return {409, "text/plain", refused};
    size_t len = 0;
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
      try { len = std::stoul(it->second); } catch (...) {}
    }

    int fd = req.fd;
    auto status = service_.Upload(path, len,
      [fd](char* buf, size_t max) -> ssize_t {
        return ::recv(fd, buf, max, 0);
      });

    if (status == services::TransferStatus::Incomplete)
      return {500, "text/plain", "transfer incomplete"};
    if (status != services::TransferStatus::Ok)
      return {500, "text/plain", "internal error"};
    return http::Response::Ok("OK", "text/plain");
  }

  return http::Response::NotFound();
}

}  // namespace controllers
