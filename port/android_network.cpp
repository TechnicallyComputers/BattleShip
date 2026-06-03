#include "android_network.h"

#if (defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE))
extern "C" int syNetReconnectMidMatchEligible(void);
extern "C" void syNetReconnectNotifyNetworkChange(void);
#endif

#if defined(__ANDROID__)

#include <atomic>
#include <jni.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>

#include "port_log.h"

static std::atomic<int> sPortAndroidNetworkPending;
static JavaVM *sPortAndroidJvm;
static jobject sPortAndroidActivityGlobal;
static std::atomic<int> sPortAndroidMonitorInstalled;
static std::atomic<int> sPortAndroidMonitorInstallPending;
static std::atomic<int> sPortAndroidMonitorDisarmPending;

extern "C" JNIEXPORT void JNICALL Java_com_jrickey_battleship_NetworkMonitor_bindContext(JNIEnv *env,
                                                                                         jclass clazz,
                                                                                         jobject context)
{
	(void)clazz;
	port_android_network_bind_context(env, context);
}

extern "C" JNIEXPORT void JNICALL
Java_com_jrickey_battleship_NetworkMonitor_nativeNotifyNetworkChange(JNIEnv *env, jclass clazz)
{
	(void)env;
	(void)clazz;
	sPortAndroidNetworkPending.store(1, std::memory_order_release);
}

static void port_android_network_jni_clear_exception(JNIEnv *env, const char *context)
{
	if ((env == nullptr) || (env->ExceptionCheck() == JNI_FALSE))
	{
		return;
	}
	port_log("SSB64 AndroidNetwork: JNI exception at %s\n", context);
	env->ExceptionClear();
}

static jobject port_android_network_get_install_context(JNIEnv *env)
{
	jclass activity_class;
	jmethodID get_app_context;
	jobject app_context;

	if ((env == nullptr) || (sPortAndroidActivityGlobal == nullptr))
	{
		return nullptr;
	}
	activity_class = env->GetObjectClass(sPortAndroidActivityGlobal);
	if (activity_class == nullptr)
	{
		port_android_network_jni_clear_exception(env, "GetObjectClass(Activity)");
		return nullptr;
	}
	get_app_context =
	    env->GetMethodID(activity_class, "getApplicationContext", "()Landroid/content/Context;");
	env->DeleteLocalRef(activity_class);
	if (get_app_context == nullptr)
	{
		port_android_network_jni_clear_exception(env, "getApplicationContext");
		return nullptr;
	}
	app_context = env->CallObjectMethod(sPortAndroidActivityGlobal, get_app_context);
	if (env->ExceptionCheck() != JNI_FALSE)
	{
		port_android_network_jni_clear_exception(env, "Call getApplicationContext");
		return nullptr;
	}
	return app_context;
}

static int port_android_network_run_install_on_main(JNIEnv *env)
{
	jclass monitor_class;
	jmethodID install_mid;
	jobject install_context;

	if (sPortAndroidMonitorInstalled.load(std::memory_order_acquire) != 0)
	{
		return 1;
	}
	if ((sPortAndroidActivityGlobal == nullptr) || (env == nullptr))
	{
		return 0;
	}
	install_context = port_android_network_get_install_context(env);
	if (install_context == nullptr)
	{
		return 0;
	}
	monitor_class = env->FindClass("com/jrickey/battleship/NetworkMonitor");
	if (monitor_class == nullptr)
	{
		port_android_network_jni_clear_exception(env, "FindClass NetworkMonitor");
		env->DeleteLocalRef(install_context);
		return 0;
	}
	install_mid = env->GetStaticMethodID(monitor_class, "install", "(Landroid/content/Context;)V");
	if (install_mid == nullptr)
	{
		port_android_network_jni_clear_exception(env, "GetStaticMethodID install");
		env->DeleteLocalRef(monitor_class);
		env->DeleteLocalRef(install_context);
		return 0;
	}
	env->CallStaticVoidMethod(monitor_class, install_mid, install_context);
	if (env->ExceptionCheck() != JNI_FALSE)
	{
		port_android_network_jni_clear_exception(env, "NetworkMonitor.install");
		env->DeleteLocalRef(monitor_class);
		env->DeleteLocalRef(install_context);
		return 0;
	}
	env->DeleteLocalRef(monitor_class);
	env->DeleteLocalRef(install_context);
	sPortAndroidMonitorInstalled.store(1, std::memory_order_release);
	return 1;
}

static void port_android_network_run_uninstall_on_main(JNIEnv *env)
{
	jclass monitor_class;
	jmethodID uninstall_mid;

	if (env == nullptr)
	{
		return;
	}
	monitor_class = env->FindClass("com/jrickey/battleship/NetworkMonitor");
	if (monitor_class == nullptr)
	{
		port_android_network_jni_clear_exception(env, "FindClass NetworkMonitor");
		return;
	}
	uninstall_mid = env->GetStaticMethodID(monitor_class, "uninstall", "()V");
	if (uninstall_mid == nullptr)
	{
		port_android_network_jni_clear_exception(env, "GetStaticMethodID uninstall");
		env->DeleteLocalRef(monitor_class);
		return;
	}
	env->CallStaticVoidMethod(monitor_class, uninstall_mid);
	port_android_network_jni_clear_exception(env, "NetworkMonitor.uninstall");
	env->DeleteLocalRef(monitor_class);
}

extern "C" void port_android_network_bind_context(JNIEnv *env, jobject activity)
{
	if (env == nullptr)
	{
		return;
	}
	if (activity != nullptr)
	{
		if (sPortAndroidActivityGlobal != nullptr)
		{
			env->DeleteGlobalRef(sPortAndroidActivityGlobal);
			sPortAndroidActivityGlobal = nullptr;
		}
		sPortAndroidActivityGlobal = env->NewGlobalRef(activity);
	}
	env->GetJavaVM(&sPortAndroidJvm);
}

extern "C" void port_android_network_try_arm_monitoring(void)
{
	if (sPortAndroidMonitorInstalled.load(std::memory_order_acquire) != 0)
	{
		return;
	}
	if (sPortAndroidMonitorDisarmPending.load(std::memory_order_acquire) != 0)
	{
		return;
	}
	if ((sPortAndroidJvm == nullptr) || (sPortAndroidActivityGlobal == nullptr))
	{
		return;
	}
	sPortAndroidMonitorInstallPending.store(1, std::memory_order_release);
}

extern "C" void port_android_network_disarm_monitoring(void)
{
	sPortAndroidMonitorInstallPending.store(0, std::memory_order_release);
	sPortAndroidMonitorDisarmPending.store(1, std::memory_order_release);
	sPortAndroidMonitorInstalled.store(0, std::memory_order_release);
	sPortAndroidNetworkPending.store(0, std::memory_order_release);
}

static void port_android_network_drain_main_thread_jni(void)
{
	JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());

	if (sPortAndroidMonitorDisarmPending.exchange(0, std::memory_order_acq_rel) != 0)
	{
		if (env != nullptr)
		{
			port_android_network_run_uninstall_on_main(env);
		}
		sPortAndroidMonitorInstallPending.store(0, std::memory_order_release);
		sPortAndroidMonitorInstalled.store(0, std::memory_order_release);
		return;
	}
	if (sPortAndroidMonitorInstallPending.exchange(0, std::memory_order_acq_rel) != 0)
	{
		if ((env == nullptr) || (port_android_network_run_install_on_main(env) == 0))
		{
			sPortAndroidMonitorInstallPending.store(1, std::memory_order_release);
		}
	}
}

extern "C" void port_android_network_drain(void)
{
	port_android_network_drain_main_thread_jni();

#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
	if (syNetReconnectMidMatchEligible() == 0)
	{
		sPortAndroidNetworkPending.store(0, std::memory_order_release);
		return;
	}
	if (sPortAndroidNetworkPending.exchange(0, std::memory_order_acq_rel) != 0)
	{
		syNetReconnectNotifyNetworkChange();
	}
#endif
}

extern "C" void port_android_network_install(JNIEnv *env, jobject activity)
{
	port_android_network_bind_context(env, activity);
}

#elif defined(__linux__)

#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>

static std::atomic<int> sPortLinuxNetlinkPending;
static std::thread sPortLinuxNetlinkThread;
static std::atomic<int> sPortLinuxNetlinkRunning;
static std::atomic<int> sPortLinuxNetlinkArmed;

static void port_linux_netlink_thread(void)
{
	while (sPortLinuxNetlinkRunning.load(std::memory_order_acquire) != 0)
	{
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}
}

extern "C" void port_android_network_bind_context(void *ctx)
{
	(void)ctx;
}

extern "C" void port_android_network_try_arm_monitoring(void)
{
#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
	const char *env = std::getenv("SSB64_NETPLAY_RECONNECT_NETLINK");
	if ((env == nullptr) || (env[0] == '\0') || (std::atoi(env) == 0))
	{
		return;
	}
	if (sPortLinuxNetlinkArmed.exchange(1, std::memory_order_acq_rel) != 0)
	{
		return;
	}
	if (sPortLinuxNetlinkRunning.exchange(1, std::memory_order_acq_rel) != 0)
	{
		return;
	}
	sPortLinuxNetlinkThread = std::thread(port_linux_netlink_thread);
#endif
}

extern "C" void port_android_network_disarm_monitoring(void)
{
#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
	if (sPortLinuxNetlinkArmed.exchange(0, std::memory_order_acq_rel) == 0)
	{
		return;
	}
	sPortLinuxNetlinkPending.store(0, std::memory_order_release);
	if (sPortLinuxNetlinkRunning.exchange(0, std::memory_order_acq_rel) != 0)
	{
		if (sPortLinuxNetlinkThread.joinable())
		{
			sPortLinuxNetlinkThread.join();
		}
	}
#endif
}

extern "C" void port_android_network_drain(void)
{
#if defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE)
	const char *env = std::getenv("SSB64_NETPLAY_RECONNECT_NETLINK");
	if ((env == nullptr) || (env[0] == '\0') || (std::atoi(env) == 0))
	{
		return;
	}
	if (syNetReconnectMidMatchEligible() == 0)
	{
		sPortLinuxNetlinkPending.store(0, std::memory_order_release);
		return;
	}
	if (sPortLinuxNetlinkPending.exchange(0, std::memory_order_acq_rel) != 0)
	{
		syNetReconnectNotifyNetworkChange();
	}
#endif
}

extern "C" void port_android_network_install(void *ctx)
{
	port_android_network_bind_context(ctx);
}

#else

extern "C" void port_android_network_bind_context(void *ctx)
{
	(void)ctx;
}
extern "C" void port_android_network_try_arm_monitoring(void) {}
extern "C" void port_android_network_disarm_monitoring(void) {}
extern "C" void port_android_network_drain(void) {}
extern "C" void port_android_network_install(void *ctx)
{
	(void)ctx;
}

#endif
