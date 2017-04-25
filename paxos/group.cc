// Copyright (c) 2016 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paxos/group.h"

#include <unistd.h>

#include "util/mutexlock.h"
#include "paxos/node_util.h"
#include "skywalker/logging.h"

namespace skywalker {

Group::Group(uint64_t node_id,
             const GroupOptions& options, Network* network)
    : node_id_(node_id),
      config_(node_id, options, network),
      instance_(&config_),
      lease_timeout_(10 * 1000 * 1000),
      retrie_master_(false),
      membership_machine_(&config_),
      master_machine_(&config_),
      mutex_(),
      cond_(&mutex_),
      propose_end_(false),
      propose_queue_(100),
      schedule_(new Schedule(options.use_master)) {
  propose_cb_ =  std::bind(&ProposeQueue::ProposeComplete,
                           &propose_queue_,
                           std::placeholders::_1,
                           std::placeholders::_2,
                           std::placeholders::_3);
  instance_.SetProposeCompleteCallback(propose_cb_);
  instance_.AddMachine(&membership_machine_);
  if (options.use_master) {
    instance_.AddMachine(&master_machine_);
  }
  for (auto& machine : options.machines) {
    instance_.AddMachine(machine);
  }
}

bool Group::Start() {
  bool res = config_.Init();
  if (res) {
    membership_machine_.Recover();
    master_machine_.Recover();
    res = instance_.Recover();
    if (res) {
      schedule_->Start();
      instance_.SetIOLoop(schedule_->IOLoop());
      instance_.SetLearnLoop(schedule_->LearnLoop());
      propose_queue_.SetIOLoop(schedule_->IOLoop());
      propose_queue_.SetCallbackLoop(schedule_->CallbackLoop());
    }
  }
  return res;
}

void Group::SyncMembership() {
  instance_.SyncData();
  int i = 0;
  while (true) {
    if (i++ > 3) {
      instance_.SyncData();
    }
    if (!membership_machine_.HasSyncMembership()) {
      MachineContext* context(
          new MachineContext(membership_machine_.machine_id()));
      ProposeHandler f(std::bind(&Group::SyncMembershipInLoop, this, context));
      bool res = NewPropose(std::move(f));
      if (res) {
        if (result_.ok() || result_.IsConflict()) {
          break;
        }
      } else {
        delete context;
      }
    } else {
      break;
    }
    usleep(30*1000);
  }
}

void Group::SyncMembershipInLoop(MachineContext* context) {
  if (!membership_machine_.HasSyncMembership()) {
    const Membership& m(membership_machine_.GetMembership());
    std::string s;
    m.SerializeToString(&s);
    instance_.OnPropose(s, context);
  } else {
    propose_cb_(context, Status::OK(), instance_.GetInstanceId());
  }
}

void Group::SyncMaster() {
  if (schedule_->MasterLoop() == nullptr) {
    return;
  }
  schedule_->MasterLoop()->QueueInLoop([this]() {
    TryBeMaster();
  });
}

void Group::TryBeMaster() {
  MasterState state(master_machine_.GetMasterState());
  uint64_t next_time = state.lease_time();
  uint64_t now = NowMicros();
  if (state.lease_time() <= now ||
      (state.node_id() == node_id_ && !retrie_master_)) {
    uint64_t lease_time = now + lease_timeout_;
    MachineContext* context(
        new MachineContext(master_machine_.machine_id(),
                           reinterpret_cast<void*>(&lease_time)));
    ProposeHandler f(std::bind(&Group::TryBeMasterInLoop, this, context));
    bool res = NewPropose(std::move(f));
    next_time = 0;
    if (res) {
      state = master_machine_.GetMasterState();
      if (state.node_id() == node_id_) {
        if (result_.ok()) {
          next_time = state.lease_time() - 50 * 1000;
        } else if (result_.IsConflict()) {
          next_time = state.lease_time();
        }
      }
    } else {
      delete context;
    }
    if (next_time == 0) {
      next_time = NowMicros() + lease_timeout_;
    }
  }
  if (retrie_master_) {
    retrie_master_ = false;
  }

  schedule_->MasterLoop()->RunAt(next_time, [this]() {
    TryBeMaster();
  });
}

void Group::TryBeMasterInLoop(MachineContext* context) {
  MasterState state(master_machine_.GetMasterState());
  if (state.lease_time() <= NowMicros() || state.node_id() == node_id_) {
    state.set_node_id(node_id_);
    state.set_lease_time(lease_timeout_);
    std::string s;
    state.SerializeToString(&s);
    instance_.OnPropose(s, context);
  } else {
    propose_cb_(context, Status::Conflict(Slice()), instance_.GetInstanceId());
  }
}

bool Group::OnPropose(const std::string& value,
                      MachineContext* context,
                      const ProposeCompleteCallback& cb) {
  return propose_queue_.Put(
      std::bind(&Instance::OnPropose, &instance_, value, context), cb);
}

bool Group::OnPropose(const std::string& value,
                      MachineContext* context,
                      ProposeCompleteCallback&& cb) {
  return propose_queue_.Put(
      std::bind(&Instance::OnPropose, &instance_, value, context),
      std::move(cb));
}

void Group::OnReceiveContent(const std::shared_ptr<Content>& c) {
  schedule_->IOLoop()->QueueInLoop([c, this]() {
    instance_.OnReceiveContent(c);
  });
}

bool Group::AddMember(const IpPort& ip,
                      const MembershipCompleteCallback& cb) {
  uint64_t node_id(MakeNodeId(ip));
  MachineContext* context(
      new MachineContext(membership_machine_.machine_id()));
  bool res = propose_queue_.Put(
      std::bind(&Group::AddMemberInLoop, this, node_id, context),
      [cb](MachineContext* c, const Status& s, uint64_t instance_id) {
    cb(s, instance_id);
    delete c;
  });
  if (!res) {
    delete context;
  }
  return res;
}

void Group::AddMemberInLoop(uint64_t node_id, MachineContext* context) {
  bool res = false;
  const Membership& temp(membership_machine_.GetMembership());
  for (int i = 0; i < temp.node_id_size(); ++i) {
    if (node_id == temp.node_id(i)) {
      res = true;
      break;
    }
  }
  if (res) {
    propose_cb_(context, Status::OK(), instance_.GetInstanceId());
  } else {
    Membership m(temp);
    m.add_node_id(node_id);
    std::string s;
    m.SerializeToString(&s);
    instance_.OnPropose(s, context);
  }
}

bool Group::RemoveMember(const IpPort& ip,
                         const MembershipCompleteCallback& cb) {
  uint64_t node_id(MakeNodeId(ip));
  MachineContext* context(
      new MachineContext(membership_machine_.machine_id()));
  bool res = propose_queue_.Put(
      std::bind(&Group::RemoveMemberInLoop, this, node_id, context),
      [cb](MachineContext* c, const Status& s, uint64_t instance_id) {
    cb(s, instance_id);
    delete c;
  });
  if (res) {
    delete context;
  }
  return res;
}

void Group::RemoveMemberInLoop(uint64_t node_id, MachineContext* context) {
  bool res = false;
  const Membership& temp(membership_machine_.GetMembership());
  Membership m;
  for (int i = 0; i < temp.node_id_size(); ++i) {
    if (node_id == temp.node_id(i)) {
     res = true;
    } else {
      m.add_node_id(temp.node_id(i));
    }
  }

  if (!res) {
    propose_cb_(context, Status::OK(), instance_.GetInstanceId());
  } else {
    std::string s;
    m.SerializeToString(&s);
    instance_.OnPropose(s, context);
  }
}

bool Group::ReplaceMember(const IpPort& new_i, const IpPort& old_i,
                          const MembershipCompleteCallback& cb) {
  uint64_t i(MakeNodeId(new_i));
  uint64_t j(MakeNodeId(old_i));
  MachineContext* context(
      new MachineContext(membership_machine_.machine_id()));
  bool res = propose_queue_.Put(
      std::bind(&Group::ReplaceMemberInLoop, this, i, j, context),
      [cb](MachineContext* c, const Status& s, uint64_t instance_id) {
    cb(s, instance_id);
    delete c;
  });
  if (!res) {
    delete context;
  }
  return res;
}

void Group::ReplaceMemberInLoop(uint64_t new_node_id, uint64_t old_node_id,
                                MachineContext* context) {
  bool new_res = false;
  bool old_res = false;
  const Membership& temp(membership_machine_.GetMembership());
  Membership m;
  for (int i = 0; i < temp.node_id_size(); ++i) {
    if (temp.node_id(i) == new_node_id) {
      new_res = true;
    }
    if (temp.node_id(i) == old_node_id) {
      old_res = true;
    } else {
      m.add_node_id(temp.node_id(i));
    }
  }

  if (new_res &&  (!old_res)) {
    propose_cb_(context, Status::OK(), instance_.GetInstanceId());
  } else {
    if (!new_res) {
      m.add_node_id(new_node_id);
    }
    std::string s;
    m.SerializeToString(&s);
    instance_.OnPropose(s, context);
  }
}

bool Group::NewPropose(ProposeHandler&& f) {
  MutexLock lock(&mutex_);
  propose_end_ = false;
  ProposeCompleteCallback cb =
      std::bind(&Group::ProposeComplete, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3);
  bool res = propose_queue_.Put(std::move(f), std::move(cb));
  if (res) {
    while (!propose_end_) {
      cond_.Wait();
    }
  }
  return res;
}

void Group::ProposeComplete(MachineContext* context,
                            const Status& result,
                            uint64_t instance_id) {
  MutexLock lock(&mutex_);
  delete context;
  result_ = result;
  propose_end_ = true;
  cond_.Signal();
  SWLog(DEBUG, "Group::ProposeComplete - %s\n", result_.ToString().c_str());
}

void Group::GetMembership(std::vector<IpPort>* result) const {
  membership_machine_.GetMembership(result);
}

void Group::SetMasterLeaseTime(uint64_t micros) {
  if (schedule_->MasterLoop()) {
    schedule_->MasterLoop()->QueueInLoop([micros, this]() {
      if (micros < (5 *1000 * 1000)) {
        lease_timeout_ = 5 * 1000 * 1000;
      } else {
        lease_timeout_ = micros;
      }
    });
  } else {
    SWLog(WARN, "Group::SetMasterLeaseTime - You don't use master.\n");
  }
}

bool Group::GetMaster(IpPort* i, uint64_t* version) const {
  return master_machine_.GetMaster(i, version);
}

bool Group::IsMaster() const {
  return master_machine_.IsMaster();
}

void Group::RetireMaster() {
  if (schedule_->MasterLoop()) {
    schedule_->MasterLoop()->QueueInLoop([this]() {
      retrie_master_ = true;
    });
  } else {
    SWLog(WARN, "Group::RetireMaster - You don't use master.\n");
  }
}

}  // namespace skywalker
