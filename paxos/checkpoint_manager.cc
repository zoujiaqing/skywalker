// Copyright (c) 2016 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paxos/checkpoint_manager.h"
#include "skywalker/logging.h"

namespace skywalker {

CheckpointManager::CheckpointManager(Config* config,
                                     StateMachineManager* manager)
    : config_(config),
      state_machine_manager_(manager) {
}

bool CheckpointManager::Init(uint64_t instance_id) {
  int res = config_->GetDB()->GetMinChosenInstanceId(&min_chosen_id_);
  if (res < 0) {
    return false;
  }
  bool success = true;
  min_chosen_id_ += 1;
  if (min_chosen_id_ < instance_id) {
    success = ReplayLog(min_chosen_id_, instance_id);
  }
  return success;
}

bool CheckpointManager::ReplayLog(uint64_t from, uint64_t to) {
  for (uint64_t instance_id = from; instance_id < to; ++instance_id) {
    std::string s;
    int res = config_->GetDB()->Get(instance_id, &s);
    if (res != 0) {
      SWLog(ERROR, "ReplayLog failed, which group_id:%" PRIu32", "
            "instance_id:%" PRIu64".\n",
            config_->GetGroupId(), instance_id);
      return false;
    }
    AcceptorState state;
    state.ParseFromString(s);
    const PaxosValue& value = state.accepted_value();
    bool success = state_machine_manager_->Execute(
        value.machine_id(), config_->GetGroupId(), instance_id,
        value.user_data(), nullptr);
    if (!success) {
      SWLog(INFO, "StateMachine execute failed, which machine_id:%d, "
            "group_id:%" PRIu32", instance_id:%" PRIu64".\n",
            value.machine_id(), config_->GetGroupId(), instance_id);
    }
  }
  return true;
}

}  // namespace skywalker
