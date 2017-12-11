// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (c) 2017 Tallence AG and the authors
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */
#include "rados-namespace-manager.h"

#include <rados/librados.hpp>

namespace librmb {

RadosNamespaceManager::~RadosNamespaceManager() {
}

bool RadosNamespaceManager::lookup_key(std::string uid, std::string *value) {
  if (uid.empty()) {
    *value = uid;
    return true;
  }

  if (!config->is_config_valid()) {
    return false;
  }

  if (!config->is_user_mapping()) {
    *value = uid;
    return true;
  }

  if (cache.find(uid) != cache.end()) {
    *value = cache[uid];
    return true;
  }

  ceph::bufferlist bl;
  std::string oid = uid;
  bool retval = false;

  // temporarily set storage namespace to config namespace
  std::string user_ns = config->get_user_ns();
  config->set_io_ctx_namespace(user_ns);
  // storage->set_namespace(config->get_user_ns());
  int err = config->read_object(oid, &bl);
  if (err >= 0 && !bl.to_str().empty()) {
    *value = bl.to_str();
    cache[uid] = *value;
    retval = true;
  }
  // reset namespace to empty
  std::string mail_namespace;
  config->set_io_ctx_namespace(mail_namespace);
  return retval;
}

bool RadosNamespaceManager::add_namespace_entry(std::string uid, std::string *value,
                                                RadosGuidGenerator *guid_generator_) {
  if (!config->is_config_valid()) {
    return false;
  }
  if (guid_generator_ == nullptr) {
    return false;
  }

  *value = guid_generator_->generate_guid();
  // temporarily set storage namespace to config namespace
  std::string user_ns = config->get_user_ns();
  config->set_io_ctx_namespace(user_ns);

  std::string oid = uid;
  ceph::bufferlist bl;
  bl.append(*value);
  bool retval = false;
  if (config->save_object(oid, bl) >= 0) {
    cache[uid] = *value;
    retval = true;
  }
  // reset namespace
  std::string mail_namespace;
  config->set_io_ctx_namespace(mail_namespace);
  return retval;
}

} /* namespace librmb */