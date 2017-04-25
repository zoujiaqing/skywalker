// Copyright (c) 2016 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SKYWALKER_INCLUDE_STATE_MACHINE_H_
#define SKYWALKER_INCLUDE_STATE_MACHINE_H_

#include <stdint.h>
#include <string>

namespace skywalker {

class MachineContext {
 public:
  int machine_id;
  void* user_data;

  MachineContext()
      : machine_id(-1),
        user_data(nullptr) {
  }

  explicit MachineContext(int id, void* data = nullptr)
      : machine_id(id),
        user_data(data) {
  }
};

class StateMachine {
 public:
  StateMachine() { }
  virtual ~StateMachine() { }

  // id > 5
  void set_machine_id(int id) { id_ = id; }
  int machine_id() const { return id_; }

  virtual bool Execute(uint32_t group_id,
                       uint64_t instance_id,
                       const std::string& value,
                       MachineContext* context = nullptr) = 0;

 private:
  int id_;

  // No copying allowed
  StateMachine(const StateMachine&);
  void operator=(const StateMachine&);
};

}  // namespace skywalker

#endif  // SKYWALKER_INCLUDE_STATE_MACHINE_H_
