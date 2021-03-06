/*
Copyright 2017-2019 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "lullaby/modules/jni/registry_jni.h"
#include "lullaby/modules/script/script_engine_binder.h"

LULLABY_JNI_CALL_CLASS_WITH_REGISTRY(ScriptEngine, nativeCreateEngine,
                                     ScriptEngineBinder, CreateEngine)

LULLABY_JNI_CALL_CLASS_WITH_REGISTRY(ScriptEngine, nativeCreateJavascriptEngine,
                                     ScriptEngineBinder, CreateJavascriptEngine)

LULLABY_JNI_CALL_CLASS_WITH_REGISTRY(ScriptEngine, nativeCreateBinder,
                                     ScriptEngineBinder, CreateBinder)
