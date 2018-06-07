// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (c) 2017-2018 Tallence AG and the authors
 * Copyright (c) 2007-2017 Dovecot authors
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */
extern "C" {
#include "lib.h"
#include "module-dir.h"
#include "str.h"
#include "hash.h"
#include "dict.h"
#include "imap-match.h"
#include "doveadm-settings.h"
#include "doveadm-mail.h"
#include "doveadm-rbox-plugin.h"
#include "mail-user.h"
}
#include "tools/rmb/rmb-commands.h"
#include "rados-cluster.h"
#include "rados-cluster-impl.h"
#include "rados-storage.h"
#include "rados-storage-impl.h"
#include "rados-dovecot-ceph-cfg.h"
#include "rados-dovecot-ceph-cfg-impl.h"

class RboxDoveadmPlugin {
 public:
  RboxDoveadmPlugin() {
    this->cluster = new librmb::RadosClusterImpl();
    this->storage = new librmb::RadosStorageImpl(cluster);
    this->config = new librmb::RadosDovecotCephCfgImpl(&storage->get_io_ctx());
  }
  ~RboxDoveadmPlugin() {
    if (config != nullptr) {
      delete config;
    }
    if (storage != nullptr) {
      storage->close_connection();
      delete storage;
    }
    if (cluster != nullptr) {
      cluster->deinit();
      delete cluster;
    }
  }
  int open_connection() {
    int ret = -1;
    if (config == nullptr) {
      return -1;
    }
    ret = storage->open_connection(config->get_pool_name(), config->get_rados_cluster_name(),
                                   config->get_rados_username());
    return ret;
  }
  int read_plugin_configuration(struct mail_user *user) {
    std::map<std::string, std::string> *map = config->get_config();
    for (std::map<std::string, std::string>::iterator it = map->begin(); it != map->end(); ++it) {
      std::string setting = it->first;
      const char *value = mail_user_plugin_getenv(user, setting.c_str());
      i_debug("read config:%s value: %s", setting.c_str(), value);
      config->update_metadata(setting, value);
    }
    config->set_config_valid(true);
    return 0;
  }

 public:
  librmb::RadosCluster *cluster;
  librmb::RadosStorage *storage;
  librmb::RadosDovecotCephCfg *config;
};

static int cmd_rmb_lspools_run(struct doveadm_mail_cmd_context *ctx, struct mail_user *user) {
  //  const char *ns_prefix = ctx->args[0];

  librmb::RmbCommands::RmbCommands::lspools();
  return 0;
}

static int cmd_rmb_config_show_run(struct doveadm_mail_cmd_context *ctx, struct mail_user *user) {
  RboxDoveadmPlugin plugin;
  plugin.read_plugin_configuration(user);
  int open = plugin.open_connection();
  if (open < 0) {
    i_error("error opening rados connection, check config: %d", open);
    return open;
  }
  std::map<std::string, std::string> opts;

  librmb::RadosConfig *dovecot_cfg = (static_cast<librmb::RadosDovecotCephCfgImpl *>(plugin.config))->get_dovecot_cfg();
  std::cout << std::endl << dovecot_cfg->to_string() << std::endl;

  opts["print_cfg"] = "true";
  librmb::RmbCommands rmb_cmds(plugin.storage, plugin.cluster, &opts);
  librmb::RadosCephConfig *cfg = (static_cast<librmb::RadosDovecotCephCfgImpl *>(plugin.config))->get_rados_ceph_cfg();
  int ret = cfg->load_cfg();
  if (ret < 0) {
    i_error("error loading config");
    return -1;
  }
  rmb_cmds.configuration(true, *cfg);
  return 0;
}
static int cmd_rmb_config_update_run(struct doveadm_mail_cmd_context *ctx, struct mail_user *user) {
  const char *update = ctx->args[0];
  i_debug("update param %s", update);
  RboxDoveadmPlugin plugin;
  plugin.read_plugin_configuration(user);
  int open = plugin.open_connection();
  if (open < 0) {
    i_error("error opening rados connection, check config: %d", open);
    return open;
  }
  std::map<std::string, std::string> opts;
  opts["update"] = update;
  librmb::RmbCommands rmb_cmds(plugin.storage, plugin.cluster, &opts);
  librmb::RadosCephConfig *cfg = (static_cast<librmb::RadosDovecotCephCfgImpl *>(plugin.config))->get_rados_ceph_cfg();
  int ret = cfg->load_cfg();
  if (ret < 0) {
    i_error("error loading config");
    return -1;
  }
  rmb_cmds.configuration(true, *cfg);
  return 0;
}
static int cmd_rmb_config_create_run(struct doveadm_mail_cmd_context *ctx, struct mail_user *user) {
  RboxDoveadmPlugin plugin;
  plugin.read_plugin_configuration(user);
  int open = plugin.open_connection();
  if (open < 0) {
    i_error("error opening rados connection, check config: %d", open);
    return open;
  }
  librmb::RadosCephConfig *cfg = (static_cast<librmb::RadosDovecotCephCfgImpl *>(plugin.config))->get_rados_ceph_cfg();
  int ret = cfg->load_cfg();
  if (ret < 0) {
    ret = cfg->save_cfg();
    if (ret < 0) {
      i_error("error creating configuration");
      return ret;
    }
    std::cout << "config created" << std::endl;
  } else {
    std::cout << "config already exist" << std::endl;
  }

  return 0;
}

static void cmd_rmb_lspools_init(struct doveadm_mail_cmd_context *ctx ATTR_UNUSED, const char *const args[]) {
  if (str_array_length(args) > 0) {
    doveadm_mail_help_name("rmb lspools");
  }
}
static void cmd_rmb_config_update_init(struct doveadm_mail_cmd_context *ctx ATTR_UNUSED, const char *const args[]) {
  if (str_array_length(args) > 1) {
    doveadm_mail_help_name("rmb config update key=value");
  }
}

struct doveadm_mail_cmd_context *cmd_rmb_lspools_alloc(void) {
  struct doveadm_mail_cmd_context *ctx;
  ctx = doveadm_mail_cmd_alloc(struct doveadm_mail_cmd_context);
  ctx->v.run = cmd_rmb_lspools_run;
  ctx->v.init = cmd_rmb_lspools_init;
  return ctx;
}
struct doveadm_mail_cmd_context *cmd_rmb_config_show_alloc(void) {
  struct doveadm_mail_cmd_context *ctx;
  ctx = doveadm_mail_cmd_alloc(struct doveadm_mail_cmd_context);
  ctx->v.run = cmd_rmb_config_show_run;
  ctx->v.init = cmd_rmb_lspools_init;
  return ctx;
}
struct doveadm_mail_cmd_context *cmd_rmb_config_create_alloc(void) {
  struct doveadm_mail_cmd_context *ctx;
  ctx = doveadm_mail_cmd_alloc(struct doveadm_mail_cmd_context);
  ctx->v.run = cmd_rmb_config_create_run;
  ctx->v.init = cmd_rmb_lspools_init;
  return ctx;
}
struct doveadm_mail_cmd_context *cmd_rmb_config_update_alloc(void) {
  struct doveadm_mail_cmd_context *ctx;
  ctx = doveadm_mail_cmd_alloc(struct doveadm_mail_cmd_context);
  ctx->v.run = cmd_rmb_config_update_run;
  ctx->v.init = cmd_rmb_config_update_init;
  return ctx;
}
