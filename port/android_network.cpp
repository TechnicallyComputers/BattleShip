#include "android_network.h"

#if (defined(SSB64_NETMENU) && defined(SSB64_NETPLAY_ICE))
extern "C" int syNetReconnectMidMatchEligible(void);
extern "C" void syNetReconnectNotifyNetworkChange(void);
extern "C" void syNetReconnectNotifyAppBackground(void);
extern "C" void syNetReconnectNotifyAppForeground(void);
#endif
extern "C" void port_watchdog_set_connect_phase_pause(int paused);

#if defined(__ANDROID__)

#include <atomic>
#include <cstdlib>
#include <jni.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>

#include "port_log.h"

static std::atomic<int> sPortAndroidNetworkPending;
/*
 * App lifecycle. SDL delivers SDL_APP_WILLENTERBACKGROUND / SDL_APP_DIDENTERFOREGROUND on
 * the Android activity thread via event watches (the queue may never be pumped again once
 * backgrounded — SDL's default BLOCK_ON_PAUSE freezes the loop). The watch therefore does
 * only two things directly, both thread-safe: quiet the hang watchdog (atomic store) and
 * flag the transition. The netreconnect notify runs from the main-thread drain — which for
 * a background may never come; the peer then converges through the netpeer silence
 * detector instead. sPortAndroidLifecycleState: 0 = foreground, 1 = background.
 */
static std::atomic<int> sPortAndroidLifecycleState;
static std::atomic<int> sPortAndroidLifecycleChanged;
static std::atomic<int> sPortAndroidLifecycleWatchInstalled;

static int PortAndroidLifecycleEventWatch(void *userdata, SDL_Event *event)
{
	(void)userdata;
	switch (event->type)
	{
	case SDL_APP_WILLENTERBACKGROUND:
		port_watchdog_set_connect_phase_pause(1);
		sPortAndroidLifecycleState.store(1, std::memory_order_release);
		sPortAndroidLifecycleChanged.store(1, std::memory_order_release);
		break;
	case SDL_APP_DIDENTERFOREGROUND:
		port_watchdog_set_connect_phase_pause(0);
		sPortAndroidLifecycleState.store(0, std::memory_order_release);
		sPortAndroidLifecycleChanged.store(1, std::memory_order_release);
		break;
	default:
		break;
	}
	return 1;
}

extern "C" int port_android_app_backgrounded(void)
{
	return sPortAndroidLifecycleState.load(std::memory_order_acquire);
}

static void port_android_lifecycle_ensure_watch(void)
{
	int expected = 0;

	if (sPortAndroidLifecycleWatchInstalled.compare_exchange_strong(expected, 1))
	{
		SDL_AddEventWatch(PortAndroidLifecycleEventWatch, nullptr);
	}
}
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
	/* Also here, not just install(): the Java NetworkMonitor.bindContext JNI entry calls
	 * this directly, and the lifecycle watch must be armed on every path into the port. */
	port_android_lifecycle_ensure_watch();
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

/*
 * SSB64_NETPLAY_ANDROID_NETMON=0 skips installing the ConnectivityManager
 * NetworkCallback. Attribution aid, not a fix: the Android SIGABRT reproduced at
 * intro tick 152 has no frame in libmain.so at all — all 14 frames map to
 * framework.jar -> JIT'd Java -> libandroid_runtime -> libart -> libbase
 * (LOG(FATAL)) -> libc abort, i.e. an ART abort raised on a thread running Java
 * framework code. This callback is the only Android JNI surface armed during a
 * match, so toggling it off is the cheapest way to confirm or clear it.
 * See docs/bugs/android_intro_art_abort_2026-08-22.md.
 */
static bool port_android_network_monitor_enabled(void)
{
	static int cached = -1;

	if (cached < 0)
	{
		const char *env = std::getenv("SSB64_NETPLAY_ANDROID_NETMON");
		cached = ((env != nullptr) && (env[0] != '\0') && (std::atoi(env) == 0)) ? 0 : 1;
		if (cached == 0)
		{
			port_log("SSB64 AndroidNet: ConnectivityManager monitor disabled (SSB64_NETPLAY_ANDROID_NETMON=0)\n");
		}
	}
	return cached != 0;
}

extern "C" void port_android_network_try_arm_monitoring(void)
{
	if (port_android_network_monitor_enabled() == false)
	{
		return;
	}
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
	/*
	 * Lifecycle first, and before the eligibility early-return: the latest state wins.
	 * If the loop blocked before a background could drain, the wake-up drain sees
	 * changed=1 with state already back at foreground and only the foreground notify
	 * runs — the missed background was covered by the peer's silence detector.
	 */
	if (sPortAndroidLifecycleChanged.exchange(0, std::memory_order_acq_rel) != 0)
	{
		if (sPortAndroidLifecycleState.load(std::memory_order_acquire) != 0)
		{
			syNetReconnectNotifyAppBackground();
		}
		else
		{
			syNetReconnectNotifyAppForeground();
		}
	}
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
	port_android_lifecycle_ensure_watch();
	port_android_network_bind_context(env, activity);
}

#elif defined(__linux__)

extern "C" int port_android_app_backgrounded(void)
{
	return 0; /* desktop: the loop never blocks on minimize */
}

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

extern "C" int port_android_app_backgrounded(void)
{
	return 0;
}
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
