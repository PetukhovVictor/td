//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpProxy.h"

#include "td/utils/base64.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Slice.h"

namespace td {

void HttpProxy::send_connect() {
  VLOG(proxy) << "Send CONNECT to proxy";
  CHECK(state_ == State::SendConnect);
  state_ = State::WaitConnectResponse;

  string host = PSTRING() << ip_address_.get_ip_str() << ':' << ip_address_.get_port();
  string proxy_authorization;
  if (!username_.empty() || !password_.empty()) {
    auto userinfo = PSTRING() << username_ << ':' << password_;
    proxy_authorization = PSTRING() << "Proxy-Authorization: basic " << td::base64_encode(userinfo) << "\r\n";
  }
  fd_.output_buffer().append(PSLICE() << "CONNECT " << host << " HTTP/1.1\r\n"
                                      << "Host: " << host << "\r\n"
                                      << proxy_authorization << "\r\n");
}

Status HttpProxy::wait_connect_response() {
  CHECK(state_ == State::WaitConnectResponse);
  auto it = fd_.input_buffer().clone();
  VLOG(proxy) << "Receive CONNECT response of size " << it.size();
  if (it.size() < 12 + 1 + 1) {
    return Status::OK();
  }
  char begin_buf[12];
  MutableSlice begin(begin_buf, 12);
  it.advance(12, begin);
  if ((begin.substr(0, 10) != "HTTP/1.1 2" && begin.substr(0, 10) != "HTTP/1.0 2") || !is_digit(begin[10]) ||
      !is_digit(begin[11])) {
    return Status::Error("Failed to connect");
  }

  size_t total_size = 12;
  char c;
  MutableSlice c_slice(&c, 1);
  while (!it.empty()) {
    it.advance(1, c_slice);
    total_size++;
    if (c == '\n') {
      break;
    }
  }
  if (it.empty()) {
    return Status::OK();
  }

  char prev = '\n';
  size_t pos = 0;
  bool found = false;
  while (!it.empty()) {
    it.advance(1, c_slice);
    total_size++;
    if (c == '\n') {
      if (pos == 0 || (pos == 1 && prev == '\r')) {
        found = true;
        break;
      }
      pos = 0;
    } else {
      pos++;
    }
    prev = c;
  }
  if (!found) {
    CHECK(it.empty());
    return Status::OK();
  }

  fd_.input_buffer().advance(total_size);
  stop();
  return Status::OK();
}

Status HttpProxy::loop_impl() {
  switch (state_) {
    case State::SendConnect:
      send_connect();
      break;
    case State::WaitConnectResponse:
      TRY_STATUS(wait_connect_response());
      break;
    default:
      UNREACHABLE();
  }
  return Status::OK();
}

}  // namespace td
