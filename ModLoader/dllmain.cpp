#include "stdafx.h"
#include "Helpers\MonoTypes.h"
#include "Helpers\Logger.hpp"
#include "Helpers\DetourHelper64.hpp"
#include <json.hpp>
using json = nlohmann::json;

namespace ModLoader {
   MonoTypes::mono_class_from_name_t            mono_class_from_name;
   MonoTypes::mono_class_get_method_from_name_t mono_class_get_method_from_name;
   MonoTypes::mono_class_get_field_from_name_t  mono_class_get_field_from_name;
   MonoTypes::mono_field_get_value_t            mono_field_get_value;
   MonoTypes::mono_runtime_invoke_t             mono_runtime_invoke;
   MonoTypes::mono_domain_assembly_open_t       mono_domain_assembly_open;
   MonoTypes::mono_assembly_get_image_t         mono_assembly_get_image;
   MonoTypes::mono_assembly_foreach_t           mono_assembly_foreach;

   DetourHelper64 detourHelper    = {};
   MonoDomain*    pGameMonoDomain = nullptr;
   bool           shouldLoadMods  = false;
   bool           isLoadingMods   = false;
   bool           hasLoadedMods   = false;

   void doQModsCompatibility() {
      auto qModsLogger = std::make_unique<Logger>(new Logger("QMods Compatibility"));
      qModsLogger->log("Loading backwards compatibility.");

      for (auto& modEntry : std::filesystem::directory_iterator("QMods")) {
         if (modEntry.is_directory()) {
            std::string modName = modEntry.path().stem().u8string();
            qModsLogger->log("Loading mod '" + modName + "'.", LogType::Info);

            for (auto& file : std::filesystem::directory_iterator(modEntry))
            {
               if (file.path().filename().u8string() == std::string("mod.json")) {
                  std::ifstream i(file);
                  if (!i)
                     continue;

                  json j;
                  try
                  {
                     i >> j;
                  } catch (const json::exception& exc) {
                     qModsLogger->log("Exception occured while parsing 'mod.json' for mod '" + modName + "'!", LogType::Warning);
                     qModsLogger->log(exc.what(), LogType::Error);
                     continue;
                  }

                  if (j.count("EntryMethod") > 0) {
                     std::string monoStrings[3];

                     size_t pos = 0;
                     std::string strEntryMethod(j["EntryMethod"].get<std::string>());
                     for (size_t i = 0; i < 3; i++)
                     {
                        pos = strEntryMethod.find(".");
                        if (pos == std::string::npos) {
                           monoStrings[i] = strEntryMethod;
                           break;
                        }

                        monoStrings[i] = strEntryMethod.substr(0, pos);
                        strEntryMethod.erase(0, pos + 1);
                     }

                     if (j.count("AssemblyName") > 0) {
                        std::string   _dll        = std::filesystem::absolute(modEntry.path() / j["AssemblyName"].get<std::string>()).u8string();
                        MonoAssembly* pModAssembly = mono_domain_assembly_open(pGameMonoDomain, _dll.c_str());
                        if (pModAssembly) {
                           MonoImage*  pMonoImage    = mono_assembly_get_image(pModAssembly);
                           MonoClass*  pTargetClass  = mono_class_from_name(pMonoImage, monoStrings[0].c_str(), monoStrings[1].c_str());
                           if (pTargetClass) {
                              MonoMethod* pTargetMethod = mono_class_get_method_from_name(pTargetClass, monoStrings[2].c_str(), 0);
                              if (pTargetMethod) {
                                 mono_runtime_invoke(pTargetMethod, nullptr, nullptr, nullptr);
                                 qModsLogger->log("'" + modName + "' was successfully loaded.", LogType::Info);
                              }
                           }
                        }
                     }
                  } else {
                     qModsLogger->log("No EntryMethod!!", LogType::Warning);
                     continue;
                  }
               }
            }
         }
      }
   }
   void loadMods() {
      isLoadingMods  = true;
      doQModsCompatibility();
      hasLoadedMods = true;
   }

   void __cdecl enumAssemblies(LPVOID _pMonoAssembly, LPVOID) {
      MonoAssembly* pMonoAssembly = (MonoAssembly*)_pMonoAssembly;

      MonoImage* pMonoImage = mono_assembly_get_image((MonoAssembly*)_pMonoAssembly);
      if (pMonoImage) {
         MonoClass* pTargetClass = mono_class_from_name(pMonoImage, "", "LargeWorldStreamer");
         if (pTargetClass) {
            MonoClassField* pTargetField = mono_class_get_field_from_name(pTargetClass, "main");
            if (pTargetField) {
               MonoClass* pSingleton_LWS;
               mono_field_get_value((MonoObject*)pTargetClass, pTargetField, &pSingleton_LWS);
               if (pSingleton_LWS) {
                  bool isInited_LWS;
                  mono_field_get_value((MonoObject*)pSingleton_LWS, pTargetField, &isInited_LWS);
                  if (isInited_LWS) {
                     shouldLoadMods = true;
                  }
               }
            }
         }
      }
   }

   MonoDomain* WINAPI hkMonoGetDomain() {
      auto& detourInfo = detourHelper.detouredFunctions["mono_domain_get"];
      detourInfo.restore();

      pGameMonoDomain = ((MonoTypes::mono_domain_get_t)detourInfo.origAddress)();
      if (!pGameMonoDomain) {
         std::cerr << "[ModLoader] MonoDomain is null!!!" << std::endl;
         return nullptr; // crash on purpose
      }

      if (!shouldLoadMods)
         mono_assembly_foreach(enumAssemblies, NULL);
      if (shouldLoadMods && !isLoadingMods)
         loadMods();
      if (!isLoadingMods)
         detourInfo.detour();

      return pGameMonoDomain;
   }

   DWORD WINAPI Init(LPVOID) {
      AllocConsole();
      freopen("ModLoader_out.txt", "w", stdout);
      freopen("ModLoader_out.txt", "w", stderr);
      freopen("CONOUT$", "w", stdout);

      HMODULE hMono = nullptr;
      while (!hMono) {
         hMono = GetModuleHandleA("mono.dll");
         Sleep(1000);
      }

      mono_class_from_name            = MonoTypes::GetMonoFunction<MonoTypes::mono_class_from_name_t>(hMono, "mono_class_from_name");
      mono_class_get_method_from_name = MonoTypes::GetMonoFunction<MonoTypes::mono_class_get_method_from_name_t>(hMono, "mono_class_get_method_from_name");
      mono_class_get_field_from_name  = MonoTypes::GetMonoFunction<MonoTypes::mono_class_get_field_from_name_t>(hMono, "mono_class_get_field_from_name");
      mono_field_get_value            = MonoTypes::GetMonoFunction<MonoTypes::mono_field_get_value_t>(hMono, "mono_field_get_value");
      mono_runtime_invoke             = MonoTypes::GetMonoFunction<MonoTypes::mono_runtime_invoke_t>(hMono, "mono_runtime_invoke");
      mono_domain_assembly_open       = MonoTypes::GetMonoFunction<MonoTypes::mono_domain_assembly_open_t>(hMono, "mono_domain_assembly_open");
      mono_assembly_get_image         = MonoTypes::GetMonoFunction<MonoTypes::mono_assembly_get_image_t>(hMono, "mono_assembly_get_image");
      mono_assembly_foreach           = MonoTypes::GetMonoFunction<MonoTypes::mono_assembly_foreach_t>(hMono, "mono_assembly_foreach");

      auto mono_domain_get = MonoTypes::GetMonoFunction<MonoTypes::mono_domain_get_t>(hMono, "mono_domain_get");
      detourHelper.detour("mono_domain_get", (DWORD64)mono_domain_get, (DWORD64)&hkMonoGetDomain);

      return TRUE;
   }
}

HANDLE tModLoaderInit = NULL;
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
      DisableThreadLibraryCalls(hModule);
      tModLoaderInit = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&ModLoader::Init, NULL, 0, NULL);
   } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
      if (tModLoaderInit)
         TerminateThread(tModLoaderInit, EXIT_FAILURE);
   }
   return TRUE;
}