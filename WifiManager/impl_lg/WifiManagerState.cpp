/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "WifiManagerState.h"
#include "UtilsJsonRpc.h"
#include "../WifiManager.h" // Need access to WifiManager::getInstance so can't use 'WifiManagerInterface.h'

using namespace WPEFramework::Plugin;
using namespace WifiManagerImpl;

WifiManagerState::WifiManagerState()
{
}

void WifiManagerState::Initialize()
{
   DBusClient &dbus = DBusClient::getInstance();
   if (getWifiInterfaceName().empty())
   {
      LOGWARN("No 'wifi' interface found");
      // TODO: throw an exception?
   }
   else
   {
      // register for status updates
      dbus.registerStatusChanged(std::bind(&WifiManagerState::statusChanged, this, std::placeholders::_1, std::placeholders::_2));
      // get current wifi status
      InterfaceStatus status;
      const std::string iname = getWifiInterfaceName();
      if (dbus.networkconfig1_GetStatus(iname, status))
      {
         updateWifiStatus(status);
      }
      else
      {
         LOGWARN("failed to get interface '%s' status", iname.c_str());
      }
   }
}

WifiManagerState::~WifiManagerState()
{
}

namespace
{
   /*
   `0`: UNINSTALLED - The device was in an installed state and was uninstalled; or, the device does not have a Wifi radio installed
   `1`: DISABLED - The device is installed but not yet enabled
   `2`: DISCONNECTED - The device is installed and enabled, but not yet connected to a network
   `3`: PAIRING - The device is in the process of pairing, but not yet connected to a network
   `4`: CONNECTING - The device is attempting to connect to a network
   `5`: CONNECTED - The device is successfully connected to a network
   */
   const std::map<InterfaceStatus, WifiState> statusToState{
       {Disabled, WifiState::DISABLED},
       {Disconnected, WifiState::DISCONNECTED},
       {Associating, WifiState::CONNECTING},
       {Dormant, WifiState::DISCONNECTED},
       {Binding, WifiState::CONNECTING},
       {Assigned, WifiState::CONNECTED},
       {Scanning, WifiState::CONNECTING}};
}

uint32_t WifiManagerState::getCurrentState(const JsonObject &parameters, JsonObject &response)
{
   // this is used by Amazon, but only 'state' is used by Amazon app and needs to be provided; the rest is not important
   LOGINFOMETHOD();
   auto lookup = statusToState.find(m_wifi_status);
   if (lookup != statusToState.end())
   {
      response["state"] = static_cast<int>(lookup->second);
   }
   else
   {
      LOGWARN("unknown state: %d", m_wifi_status);
      returnResponse(false);
   }
   returnResponse(true);
}

uint32_t WifiManagerState::getConnectedSSID(const JsonObject &parameters, JsonObject &response) const
{
   // this is used by Amazon, but only 'ssid' is used by Amazon app and needs to be returned; the rest is not important
   LOGINFOMETHOD();

   const std::string &wifiInterface = getWifiInterfaceName();
   bool ret = false;
   if (!wifiInterface.empty())
   {
      std::string netid;
      if (DBusClient::getInstance().networkconfig1_GetParam(wifiInterface, "netid", netid))
      {
         size_t pos = netid.find(":");
         if (pos != std::string::npos)
         {
            response["ssid"] = netid.substr(pos + 1);
            ret = true;
         }
         else
         {
            LOGWARN("failed to parse ssid from netid");
         }
      }
      else
      {
         LOGWARN("failed to retrieve wifi netid param");
      }
   }

   // only 'ssid' is used by Amazon app and needs to be returned; the rest can be empty for now
   response["bssid"] = string("");
   response["rate"] = string("");
   response["noise"] = string("");
   response["security"] = string("");
   response["signalStrength"] = string("");
   response["frequency"] = string("");
   returnResponse(ret);
}

void WifiManagerState::statusChanged(const std::string &interface, InterfaceStatus status)
{
   if (interface == getWifiInterfaceName())
   {
      updateWifiStatus(status);
   }
}

void WifiManagerState::updateWifiStatus(WifiManagerImpl::InterfaceStatus status)
{
   {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_wifi_status = status;
   }
   auto lookup = statusToState.find(m_wifi_status);
   if (lookup != statusToState.end())
   {
      // Hardcode 'isLNF' for the moment (at the moment, the same is done in default rdk implementation)
      WifiManager::getInstance().onWIFIStateChanged(lookup->second, false);
   }
   else
   {
      LOGWARN("unknown state: %d", m_wifi_status);
   }
}

uint32_t WifiManagerState::setEnabled(const JsonObject &parameters, JsonObject &response)
{
   LOGINFOMETHOD();
   returnResponse(false);
}

uint32_t WifiManagerState::getSupportedSecurityModes(const JsonObject &parameters, JsonObject &response)
{
   LOGINFOMETHOD();
   returnResponse(false);
}

const std::string WifiManagerState::fetchWifiInterfaceName()
{
   DBusClient &dbus = DBusClient::getInstance();
   std::vector<std::string> interfaces;
   if (dbus.networkconfig1_GetInterfaces(interfaces))
   {
      for (auto &intf : interfaces)
      {
         std::string type;
         if (dbus.networkconfig1_GetParam(intf, "type", type) && type == "wifi")
         {
            return intf;
         }
      }
   }
   else
   {
      LOGWARN("failed to fetch interfaces via networkconfig1_GetInterfaces");
   }
   return "";
}

const std::string &WifiManagerState::getWifiInterfaceName()
{
   static std::mutex interface_name_mutex;
   std::lock_guard<std::mutex> lock(interface_name_mutex);
   static std::string name = WifiManagerState::fetchWifiInterfaceName();
   return name;
}