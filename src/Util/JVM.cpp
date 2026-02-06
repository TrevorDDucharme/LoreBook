#include <Util/JVM.hpp>
#include <filesystem>
#include <vector>
#include <cstdio>
#include <cstring>

static JNIEnv *javaEnv = nullptr;
static JavaVM *jvm = nullptr;

bool initializeJavaVM()
{
	JavaVMInitArgs vm_args;
	JavaVMOption options[3];

	std::string classPaths = "";

	if (classPaths.empty())
	{
		fprintf(stderr, "No class paths found in build directory.\n");
		// Continue anyway with default classpath
		classPaths = ".";
	}

	fprintf(stdout, "Class path: -Djava.class.path=%s\n", classPaths.c_str());

	// Set up VM options with classpath
	std::string classpathOption = "-Djava.class.path=" + classPaths;
	options[0].optionString = const_cast<char *>(classpathOption.c_str());

	vm_args.version = JNI_VERSION_1_8;
	vm_args.nOptions = 1;
	vm_args.options = options;
	vm_args.ignoreUnrecognized = true; // Allow unrecognized options for debugging
	try
	{
		jint res = JNI_CreateJavaVM(&jvm, (void **)&javaEnv, &vm_args);
		if (res != JNI_OK)
		{
			switch (res)
			{
			case JNI_ERR:
				fprintf(stderr, "JNI_CreateJavaVM: Unknown error (JNI_ERR)\n");
				break;
			case JNI_EDETACHED:
				fprintf(stderr, "JNI_CreateJavaVM: Thread detached from the VM (JNI_EDETACHED)\n");
				break;
			case JNI_EVERSION:
				fprintf(stderr, "JNI_CreateJavaVM: JNI version error (JNI_EVERSION)\n");
				break;
			case JNI_ENOMEM:
				fprintf(stderr, "JNI_CreateJavaVM: Not enough memory (JNI_ENOMEM)\n");
				break;
			case JNI_EEXIST:
				fprintf(stderr, "JNI_CreateJavaVM: VM already exists (JNI_EEXIST)\n");
				break;
			case JNI_EINVAL:
				fprintf(stderr, "JNI_CreateJavaVM: Invalid arguments (JNI_EINVAL)\n");
				break;
			default:
				fprintf(stderr, "JNI_CreateJavaVM: Unknown error code %d\n", res);
			}
			return false;
		}
	}
	catch (const std::exception &e)
	{
		fprintf(stderr, "JNI_CreateJavaVM: Exception during initialization: %s\n", e.what());
		return false;
	}

	return true;
}

void destroyJavaVM()
{
	if (javaEnv && jvm)
	{
		jvm->DestroyJavaVM();
		javaEnv = nullptr;
		jvm = nullptr;
	}
}

JNIEnv *getJavaEnv()
{
	return javaEnv;
}

JavaVM *getJavaVM()
{
	return jvm;
}

bool isJavaVMInitialized()
{
	return javaEnv != nullptr && jvm != nullptr;
}
