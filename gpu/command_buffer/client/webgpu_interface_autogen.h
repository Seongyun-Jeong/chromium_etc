// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is auto-generated from
// gpu/command_buffer/build_webgpu_cmd_buffer.py
// It's formatted by clang-format using chromium coding style:
//    clang-format -i -style=chromium filename
// DO NOT EDIT!

// This file is included by webgpu_interface.h to declare the
// GL api functions.
#ifndef GPU_COMMAND_BUFFER_CLIENT_WEBGPU_INTERFACE_AUTOGEN_H_
#define GPU_COMMAND_BUFFER_CLIENT_WEBGPU_INTERFACE_AUTOGEN_H_

virtual void AssociateMailbox(GLuint device_id,
                              GLuint device_generation,
                              GLuint id,
                              GLuint generation,
                              GLuint usage,
                              MailboxFlags flags,
                              const GLbyte* mailbox) = 0;
virtual void DissociateMailbox(GLuint texture_id,
                               GLuint texture_generation) = 0;
virtual void DissociateMailboxForPresent(GLuint device_id,
                                         GLuint device_generation,
                                         GLuint texture_id,
                                         GLuint texture_generation) = 0;
#endif  // GPU_COMMAND_BUFFER_CLIENT_WEBGPU_INTERFACE_AUTOGEN_H_
