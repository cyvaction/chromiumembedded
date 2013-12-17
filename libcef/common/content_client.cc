// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/common/content_client.h"
#include "include/cef_stream.h"
#include "include/cef_version.h"
#include "libcef/browser/content_browser_client.h"
#include "libcef/common/scheme_registrar_impl.h"
#include "libcef/common/scheme_registration.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/common/content_switches.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/common/user_agent/user_agent_util.h"

namespace {

CefContentClient* g_content_client = NULL;

const char kInterposeLibraryPath[] =
    "@executable_path/../../../libplugin_carbon_interpose.dylib";

}  // namespace

CefContentClient::CefContentClient(CefRefPtr<CefApp> application)
    : application_(application),
      pack_loading_disabled_(false),
      allow_pack_file_load_(false),
      scheme_info_list_locked_(false) {
  DCHECK(!g_content_client);
  g_content_client = this;
}

CefContentClient::~CefContentClient() {
  g_content_client = NULL;
}

// static
CefContentClient* CefContentClient::Get() {
  return g_content_client;
}

void CefContentClient::AddAdditionalSchemes(
    std::vector<std::string>* standard_schemes,
    std::vector<std::string>* savable_schemes) {
  DCHECK(!scheme_info_list_locked_);

  if (application_.get()) {
    CefRefPtr<CefSchemeRegistrarImpl> schemeRegistrar(
        new CefSchemeRegistrarImpl());
    application_->OnRegisterCustomSchemes(schemeRegistrar.get());
    schemeRegistrar->GetStandardSchemes(standard_schemes);

    // No references to the registar should be kept.
    schemeRegistrar->Detach();
    DCHECK(schemeRegistrar->VerifyRefCount());
  }

  scheme::AddInternalSchemes(standard_schemes);

  scheme_info_list_locked_ = true;
}

std::string CefContentClient::GetUserAgent() const {
  std::string product_version;

  static CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kProductVersion)) {
    product_version =
        command_line.GetSwitchValueASCII(switches::kProductVersion);
  } else {
    product_version = base::StringPrintf("Chrome/%d.%d.%d.%d",
        CHROME_VERSION_MAJOR, CHROME_VERSION_MINOR, CHROME_VERSION_BUILD,
        CHROME_VERSION_PATCH);
  }

  return webkit_glue::BuildUserAgentFromProduct(product_version);
}

base::string16 CefContentClient::GetLocalizedString(int message_id) const {
  base::string16 value =
      ResourceBundle::GetSharedInstance().GetLocalizedString(message_id);
  if (value.empty())
    LOG(ERROR) << "No localized string available for id " << message_id;

  return value;
}

base::StringPiece CefContentClient::GetDataResource(
    int resource_id,
    ui::ScaleFactor scale_factor) const {
  base::StringPiece value =
      ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
          resource_id, scale_factor);
  if (value.empty())
    LOG(ERROR) << "No data resource available for id " << resource_id;

  return value;
}

gfx::Image& CefContentClient::GetNativeImageNamed(int resource_id) const {
  gfx::Image& value =
      ResourceBundle::GetSharedInstance().GetNativeImageNamed(resource_id);
  if (value.IsEmpty())
    LOG(ERROR) << "No native image available for id " << resource_id;

  return value;
}

#if defined(OS_MACOSX) && !defined(OS_IOS)
std::string CefContentClient::GetCarbonInterposePath() const {
  return std::string(kInterposeLibraryPath);
}
#endif

void CefContentClient::AddCustomScheme(const SchemeInfo& scheme_info) {
  DCHECK(!scheme_info_list_locked_);
  scheme_info_list_.push_back(scheme_info);

  if (CefContentBrowserClient::Get()) {
    CefContentBrowserClient::Get()->RegisterCustomScheme(
        scheme_info.scheme_name);
  }
}

const CefContentClient::SchemeInfoList* CefContentClient::GetCustomSchemes() {
  DCHECK(scheme_info_list_locked_);
  return &scheme_info_list_;
}

bool CefContentClient::HasCustomScheme(const std::string& scheme_name) {
  DCHECK(scheme_info_list_locked_);
  if (scheme_info_list_.empty())
    return false;

  SchemeInfoList::const_iterator it = scheme_info_list_.begin();
  for (; it != scheme_info_list_.end(); ++it) {
    if (it->scheme_name == scheme_name)
      return true;
  }

  return false;
}

base::FilePath CefContentClient::GetPathForResourcePack(
    const base::FilePath& pack_path,
    ui::ScaleFactor scale_factor) {
  // Only allow the cef pack file to load.
  if (!pack_loading_disabled_ && allow_pack_file_load_)
    return pack_path;
  return base::FilePath();
}

base::FilePath CefContentClient::GetPathForLocalePack(
    const base::FilePath& pack_path,
    const std::string& locale) {
  if (!pack_loading_disabled_)
    return pack_path;
  return base::FilePath();
}

gfx::Image CefContentClient::GetImageNamed(int resource_id) {
  return gfx::Image();
}

gfx::Image CefContentClient::GetNativeImageNamed(
    int resource_id,
    ui::ResourceBundle::ImageRTL rtl) {
  return gfx::Image();
}

base::RefCountedStaticMemory* CefContentClient::LoadDataResourceBytes(
    int resource_id,
    ui::ScaleFactor scale_factor) {
  return NULL;
}

bool CefContentClient::GetRawDataResource(int resource_id,
                                          ui::ScaleFactor scale_factor,
                                          base::StringPiece* value) {
  if (application_.get()) {
    CefRefPtr<CefResourceBundleHandler> handler =
        application_->GetResourceBundleHandler();
    if (handler.get()) {
      void* data = NULL;
      size_t data_size = 0;
      if (handler->GetDataResource(resource_id, data, data_size))
        *value = base::StringPiece(static_cast<char*>(data), data_size);
    }
  }

  return (pack_loading_disabled_ || !value->empty());
}

bool CefContentClient::GetLocalizedString(int message_id,
                                          base::string16* value) {
  if (application_.get()) {
    CefRefPtr<CefResourceBundleHandler> handler =
        application_->GetResourceBundleHandler();
    if (handler.get()) {
      CefString cef_str;
      if (handler->GetLocalizedString(message_id, cef_str))
        *value = cef_str;
    }
  }

  return (pack_loading_disabled_ || !value->empty());
}

scoped_ptr<gfx::Font> CefContentClient::GetFont(
    ui::ResourceBundle::FontStyle style) {
  return scoped_ptr<gfx::Font>();
}
