// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/services/sharing/nearby/platform/fake_network_context.h"

#include "base/test/bind.h"
#include "chrome/services/sharing/nearby/platform/fake_tcp_connected_socket.h"
#include "chrome/services/sharing/nearby/platform/fake_tcp_server_socket.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/net_errors.h"

namespace location {
namespace nearby {
namespace chrome {

FakeNetworkContext::FakeNetworkContext(
    const net::IPEndPoint& default_local_addr)
    : default_local_addr_(default_local_addr) {}

FakeNetworkContext::~FakeNetworkContext() = default;

void FakeNetworkContext::SetCreateServerSocketCallExpectations(
    size_t expected_num_create_server_socket_calls,
    base::OnceClosure on_all_create_server_socket_calls_queued) {
  expected_num_create_server_socket_calls_ =
      expected_num_create_server_socket_calls;
  if (expected_num_create_server_socket_calls == 0) {
    std::move(on_all_create_server_socket_calls_queued).Run();
  } else {
    on_all_create_server_socket_calls_queued_ =
        std::move(on_all_create_server_socket_calls_queued);
  }
}

void FakeNetworkContext::SetCreateConnectedSocketCallExpectations(
    size_t expected_num_create_connected_socket_calls,
    base::OnceClosure on_all_create_connected_socket_calls_queued) {
  expected_num_create_connected_socket_calls_ =
      expected_num_create_connected_socket_calls;
  if (expected_num_create_connected_socket_calls == 0) {
    std::move(on_all_create_connected_socket_calls_queued).Run();
  } else {
    on_all_create_connected_socket_calls_queued_ =
        std::move(on_all_create_connected_socket_calls_queued);
  }
}

void FakeNetworkContext::FinishNextCreateServerSocket(int32_t result) {
  DCHECK(!pending_create_server_socket_callbacks_.empty());
  CreateCallback callback =
      std::move(pending_create_server_socket_callbacks_.front());
  pending_create_server_socket_callbacks_.pop_front();

  std::move(callback).Run(result);
}

void FakeNetworkContext::FinishNextCreateConnectedSocket(int32_t result) {
  DCHECK(!pending_create_connected_socket_callbacks_.empty());
  CreateCallback callback =
      std::move(pending_create_connected_socket_callbacks_.front());
  pending_create_connected_socket_callbacks_.pop_front();

  std::move(callback).Run(result);
}

void FakeNetworkContext::CreateTCPServerSocket(
    const net::IPEndPoint& local_addr,
    uint32_t backlog,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
    mojo::PendingReceiver<network::mojom::TCPServerSocket> socket,
    CreateTCPServerSocketCallback callback) {
  pending_create_server_socket_callbacks_.push_back(base::BindLambdaForTesting(
      [local_addr, socket = std::move(socket),
       callback = std::move(callback)](int32_t result) mutable {
        if (result != net::OK) {
          std::move(callback).Run(result, /*local_addr_out=*/absl::nullopt);
          return;
        }

        mojo::MakeSelfOwnedReceiver(std::make_unique<FakeTcpServerSocket>(),
                                    std::move(socket));

        std::move(callback).Run(result, local_addr);
      }));

  DCHECK_GE(expected_num_create_server_socket_calls_,
            pending_create_server_socket_callbacks_.size());

  if (pending_create_server_socket_callbacks_.size() ==
      expected_num_create_server_socket_calls_) {
    DCHECK(on_all_create_server_socket_calls_queued_);
    std::move(on_all_create_server_socket_calls_queued_).Run();
  }
}

void FakeNetworkContext::CreateTCPConnectedSocket(
    const absl::optional<net::IPEndPoint>& local_addr,
    const net::AddressList& remote_addr_list,
    network::mojom::TCPConnectedSocketOptionsPtr tcp_connected_socket_options,
    const net::MutableNetworkTrafficAnnotationTag& traffic_annotation,
    mojo::PendingReceiver<network::mojom::TCPConnectedSocket> socket,
    mojo::PendingRemote<network::mojom::SocketObserver> observer,
    CreateTCPConnectedSocketCallback callback) {
  pending_create_connected_socket_callbacks_.push_back(
      base::BindLambdaForTesting(
          [local_addr = local_addr.value_or(default_local_addr_),
           remote_addr = remote_addr_list[0], socket = std::move(socket),
           callback = std::move(callback)](int32_t result) mutable {
            if (result != net::OK) {
              std::move(callback).Run(result, /*local_addr=*/absl::nullopt,
                                      /*peer_addr=*/absl::nullopt,
                                      mojo::ScopedDataPipeConsumerHandle(),
                                      mojo::ScopedDataPipeProducerHandle());
              return;
            }

            mojo::ScopedDataPipeProducerHandle receive_pipe_producer_handle;
            mojo::ScopedDataPipeConsumerHandle receive_pipe_consumer_handle;
            DCHECK_EQ(MOJO_RESULT_OK,
                      mojo::CreateDataPipe(/*options=*/nullptr,
                                           receive_pipe_producer_handle,
                                           receive_pipe_consumer_handle));
            mojo::ScopedDataPipeProducerHandle send_pipe_producer_handle;
            mojo::ScopedDataPipeConsumerHandle send_pipe_consumer_handle;
            DCHECK_EQ(MOJO_RESULT_OK,
                      mojo::CreateDataPipe(/*options=*/nullptr,
                                           send_pipe_producer_handle,
                                           send_pipe_consumer_handle));
            mojo::MakeSelfOwnedReceiver(
                std::make_unique<FakeTcpConnectedSocket>(
                    std::move(receive_pipe_producer_handle),
                    std::move(send_pipe_consumer_handle)),
                std::move(socket));

            std::move(callback).Run(result, local_addr, remote_addr,
                                    std::move(receive_pipe_consumer_handle),
                                    std::move(send_pipe_producer_handle));
          }));

  DCHECK_GE(expected_num_create_connected_socket_calls_,
            pending_create_connected_socket_callbacks_.size());

  if (pending_create_connected_socket_callbacks_.size() ==
      expected_num_create_connected_socket_calls_) {
    DCHECK(on_all_create_connected_socket_calls_queued_);
    std::move(on_all_create_connected_socket_calls_queued_).Run();
  }
}

}  // namespace chrome
}  // namespace nearby
}  // namespace location
