#pragma once
#include <jni.h>

bool initializeJavaVM();
void destroyJavaVM();
JNIEnv* getJavaEnv();
JavaVM* getJavaVM();
bool isJavaVMInitialized();
