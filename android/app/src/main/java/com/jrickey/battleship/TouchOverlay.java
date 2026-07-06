package com.jrickey.battleship;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

/**
 * On-screen touch controls for the SSB64 port.
 *
 * Layout (landscape):
 *   - Left half: fixed-anchor virtual analog stick ({@link AnalogStickView}).
 *   - Right half: face-button cluster.
 *   - Top corners: shoulder/trigger buttons (Z, R) and the menu hamburger.
 *
 *        [≡]                                          [Z]    [R]
 *
 *
 *                                                        [B]    [A]
 *                                                            [Start]
 *
 * Mappings (match libultraship's N64-to-SDL defaults in
 * ControllerDefaultMappings.cpp):
 *
 *     A     = SDL_CONTROLLER_BUTTON_A
 *     B     = SDL_CONTROLLER_BUTTON_B
 *     Start = SDL_CONTROLLER_BUTTON_START
 *     Z     = SDL_CONTROLLER_AXIS_TRIGGERLEFT  (full positive, axis-mapped — Z is a digital button on real N64, but LUS maps it from a controller trigger axis)
 *     R     = SDL_CONTROLLER_AXIS_TRIGGERRIGHT (full positive — same story)
 *     Menu  = SDL_KEYDOWN F1 (matches Gui.cpp's TOGGLE_BTN)
 *
 * LUS's SDLButtonToAnyMapping picks the virtual gamepad up via the
 * Xbox-360 vendor/product signature attached on the native side
 * (port/android_touch_overlay.cpp); user-configured remappings via the
 * LUS controller menu still apply.
 */
public final class TouchOverlay {

    /** Native methods implemented in port/android_touch_overlay.cpp. */
    public static native void setButton(int sdlButton, boolean down);
    public static native void setAxis(int sdlAxis, int value);
    /** Toggle the LUS menu (deferred to SDL_main, see port/gameloop.cpp). */
    public static native void toggleMenu();
    /** Is the LUS menu currently visible? Polled to hide the overlay so
     *  touches pass through to ImGui. */
    public static native boolean isMenuVisible();

    /** Trigger-axis convention shared by C++ + Java.
     *
     *  SDL_GameController normalizes our virtual joystick's trigger axes
     *  (a4 = lefttrigger, a5 = righttrigger) from the raw range
     *  -32768..+32767 to the GameController range 0..32767. With that
     *  normalization, raw -32768 = released, raw +32767 = fully pressed.
     *  Sending raw 0 lands the trigger at ~50% pressed — which LUS reads
     *  as N64 Z/R held the entire time. */
    public static final int TRIGGER_RELEASED = -32768;
    public static final int TRIGGER_PRESSED  =  32767;

    /** Convenience helper: pressed-or-released semantics for trigger axes. */
    public static void setTrigger(int sdlAxis, boolean pressed) {
        setAxis(sdlAxis, pressed ? TRIGGER_PRESSED : TRIGGER_RELEASED);
    }

    /** SDL_GameControllerButton mirrors. */
    public static final int SDL_CONTROLLER_BUTTON_A             = 0;
    public static final int SDL_CONTROLLER_BUTTON_B             = 1;
    public static final int SDL_CONTROLLER_BUTTON_START         = 6;

    /** SDL_GameControllerAxis mirrors. Triggers go 0..32767. */
    public static final int SDL_CONTROLLER_AXIS_TRIGGERLEFT     = 4;
    public static final int SDL_CONTROLLER_AXIS_TRIGGERRIGHT    = 5;

    private TouchOverlay() { /* static */ }

    /* Lifecycle handles for uninstall(). The InputManager service and the
     * main-looper Handler both hold strong references to what we register
     * with them — without explicit unregistration the whole overlay View
     * hierarchy leaks on every Activity recreation, and the menu poller
     * keeps firing forever. */
    private static InputManager sInputManager;
    private static InputManager.InputDeviceListener sDeviceListener;
    private static Handler sUiHandler;
    private static Runnable sMenuPoller;

    /** Tear down listeners/pollers registered by install(). Call from the
     *  Activity's onDestroy. */
    public static void uninstall() {
        if (sInputManager != null && sDeviceListener != null) {
            sInputManager.unregisterInputDeviceListener(sDeviceListener);
        }
        sInputManager = null;
        sDeviceListener = null;
        if (sUiHandler != null && sMenuPoller != null) {
            sUiHandler.removeCallbacks(sMenuPoller);
        }
        sUiHandler = null;
        sMenuPoller = null;
    }

    /**
     * Build the overlay and addContentView() it onto the Activity. Must
     * be called AFTER super.onCreate so SDLActivity has already installed
     * its surface as the root content view.
     */
    public static void install(Activity activity) {
        FrameLayout root = new FrameLayout(activity);

        // Two sibling layers under `root`, so we can show/hide them
        // independently:
        //
        //   * gameplayLayer — analog stick + face cluster + shoulders + Start.
        //     Hidden when (a) a physical gamepad is paired (the user is
        //     playing with the real pad and the overlay would just steal
        //     touches), or (b) the LUS menu is open (touches need to fall
        //     through to ImGui for menu navigation).
        //
        //   * menuButton — just the hamburger. Auto-hides on the same two
        //     conditions as gameplayLayer: a physical gamepad is in control
        //     (its Select button opens the LUS menu, so the touch shortcut
        //     is redundant), or the menu is already open (taps need to fall
        //     through to ImGui).
        //
        // Both layers now share identical visibility rules — see
        // refreshOverlayVisibility(). They're kept as separate views only
        // because they group logically distinct controls (gameplay vs. menu).
        FrameLayout gameplayLayer = new FrameLayout(activity);
        FrameLayout menuLayer     = new FrameLayout(activity);

        // Left half of gameplayLayer: analog stick capture region.
        AnalogStickView stick = new AnalogStickView(activity);
        stick.setLayoutParams(new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
        FrameLayout stickHost = new FrameLayout(activity);
        stickHost.addView(stick);
        gameplayLayer.addView(stickHost, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT,
            Gravity.START | Gravity.FILL_VERTICAL));
        // Pin the stick capture region to the left half of the screen, and
        // re-derive it on every layout. BattleShipActivity survives
        // screenSize/smallestScreenSize config changes (see manifest
        // android:configChanges), so a one-shot width goes stale on a live
        // resize (multi-window, foldable, free-form), leaving the fixed anchor
        // centered in a dead or oversized region. Listening on the parent's
        // layout keeps the half-width correct across resizes; the width guard
        // ignores pre-layout 0-width passes and prevents a re-layout loop.
        gameplayLayer.addOnLayoutChangeListener(
            (v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
                int half = (right - left) / 2;
                ViewGroup.LayoutParams lp = stickHost.getLayoutParams();
                if (half > 0 && lp.width != half) {
                    lp.width = half;
                    stickHost.setLayoutParams(lp);
                }
            });

        // ── Face cluster (right thumb) ──────────────────────────────────
        addBtnButton(activity, gameplayLayer, "A",     SDL_CONTROLLER_BUTTON_A,
                     R.drawable.btn_n64_a, 120,
                     Gravity.BOTTOM | Gravity.END,    24,  60);
        addBtnButton(activity, gameplayLayer, "B",     SDL_CONTROLLER_BUTTON_B,
                     R.drawable.btn_n64_b, 110,
                     Gravity.BOTTOM | Gravity.END,   150,  90);

        // Start at bottom-center — well away from A to avoid mistaps.
        addBtnButton(activity, gameplayLayer, "Start", SDL_CONTROLLER_BUTTON_START,
                     R.drawable.btn_n64_start, 70,
                     Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL, 0, 24);

        // ── Shoulder/trigger row (top-right corner) ─────────────────────
        // Z and R map to trigger axes; see setTrigger() helper for the
        // raw-value convention.
        addAxisButton(activity, gameplayLayer, "Z",
                      SDL_CONTROLLER_AXIS_TRIGGERLEFT,
                      R.drawable.btn_n64_z, 90,
                      Gravity.TOP | Gravity.END,   130, 24);
        addAxisButton(activity, gameplayLayer, "R",
                      SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                      R.drawable.btn_n64_r, 90,
                      Gravity.TOP | Gravity.END,    24, 24);

        // ── Menu (top-left corner) — lives in its own layer. ────────────
        addMenuButton(activity, menuLayer, R.drawable.btn_n64_menu, 70,
                      Gravity.TOP | Gravity.START, 24, 24);

        root.addView(gameplayLayer, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(menuLayer, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));

        activity.addContentView(root,
            new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        installControllerWatcher(activity, gameplayLayer, menuLayer);
        installMenuVisibilityPoller(activity, gameplayLayer, menuLayer);
    }

    /**
     * Single source of truth for overlay visibility. Both layers are hidden
     * when either condition holds:
     *
     *   - a physical gamepad is in control — its buttons replace the on-screen
     *     stick/face cluster, and its Select button opens the LUS menu, so the
     *     hamburger is redundant too; or
     *   - the LUS menu is open — touches must fall through to ImGui, otherwise
     *     the stick area swallows taps on menu items.
     *
     * Idempotent: {@link View#setVisibility} no-ops when the value is
     * unchanged, so this is safe to call every poll tick and on every
     * input-device change.
     */
    private static void refreshOverlayVisibility(View gameplayLayer, View menuLayer) {
        boolean menuOpen;
        try {
            menuOpen = isMenuVisible();
        } catch (UnsatisfiedLinkError e) {
            // Native side not yet ready (pre-SDL_main) — treat menu as closed.
            menuOpen = false;
        }
        int vis = (menuOpen || countGamepads() > 0) ? View.GONE : View.VISIBLE;
        gameplayLayer.setVisibility(vis);
        menuLayer.setVisibility(vis);
    }

    /**
     * Poll overlay visibility on a 100ms cadence. We poll because the menu
     * can close from inside (X button, ESC), not just from our hamburger tap,
     * and neither event pushes a signal to Java. 100ms is cheap and
     * imperceptible. The actual show/hide decision lives in
     * {@link #refreshOverlayVisibility}.
     */
    private static void installMenuVisibilityPoller(Activity activity,
                                                    View gameplayLayer,
                                                    View menuLayer) {
        final Handler ui = new Handler(Looper.getMainLooper());
        Runnable poller = new Runnable() {
            @Override public void run() {
                refreshOverlayVisibility(gameplayLayer, menuLayer);
                ui.postDelayed(this, 100);
            }
        };
        sUiHandler = ui;
        sMenuPoller = poller;
        ui.postDelayed(poller, 100);
    }

    /**
     * Auto-hide the whole overlay (stick + face buttons AND the menu
     * hamburger) when a physical controller is paired. SDL2's
     * SDLControllerManager reports paired controllers to the native side
     * independently of our virtual joystick, so without this LUS sees two
     * controllers (real + virtual) and the user gets duplicate input.
     *
     * The menu hamburger hides along with the gameplay controls: a paired
     * pad's Select button opens the LUS menu (matching libultraship's
     * default), so the on-screen shortcut is redundant. Both layers share
     * one visibility rule — see {@link #refreshOverlayVisibility}.
     *
     * Detection is via Android's InputManager — we count every device
     * whose source mask includes SOURCE_GAMEPAD or SOURCE_JOYSTICK and
     * isn't the per-app "Virtual" device. logcat tag `ssb64.touch`
     * dumps every enumeration so you can debug missing-pad cases.
     */
    private static final String TAG = "ssb64.touch";

    private static void installControllerWatcher(Activity activity,
                                                 View gameplayLayer,
                                                 View menuLayer) {
        Object svc = activity.getSystemService(Context.INPUT_SERVICE);
        if (!(svc instanceof InputManager)) {
            Log.w(TAG, "InputManager unavailable, overlay always visible");
            return;
        }
        final InputManager im = (InputManager) svc;
        final Handler ui = new Handler(Looper.getMainLooper());

        Runnable refresh = () -> {
            int count = countGamepads();
            Log.i(TAG, "controller-watch: gamepads=" + count
                     + " overlay=" + (count > 0 ? "hidden" : "shown"));
            refreshOverlayVisibility(gameplayLayer, menuLayer);
        };

        ui.post(refresh);

        InputManager.InputDeviceListener listener = new InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id)   { Log.i(TAG, "input device added id=" + id);   ui.post(refresh); }
            @Override public void onInputDeviceRemoved(int id) { Log.i(TAG, "input device removed id=" + id); ui.post(refresh); }
            @Override public void onInputDeviceChanged(int id) { Log.i(TAG, "input device changed id=" + id); ui.post(refresh); }
        };
        sInputManager = im;
        sDeviceListener = listener;
        im.registerInputDeviceListener(listener, ui);
    }

    private static int countGamepads() {
        int count = 0;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice dev = InputDevice.getDevice(id);
            if (dev == null) continue;
            int sources = dev.getSources();
            boolean isGamepad =
                (sources & InputDevice.SOURCE_GAMEPAD)  == InputDevice.SOURCE_GAMEPAD
             || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
            // Android's internal "Virtual" device on every app process
            // advertises SOURCE_KEYBOARD only, but some OEM ROMs flag it
            // as SOURCE_GAMEPAD too. Filter by name so a pair-less device
            // doesn't auto-hide the overlay.
            String name = dev.getName();
            boolean isBuiltinVirtual = (name != null) &&
                (name.equalsIgnoreCase("Virtual") || name.startsWith("uinput"));
            Log.d(TAG, "  device id=" + id + " name='" + name + "' sources=0x"
                + Integer.toHexString(sources) + " gamepad=" + isGamepad
                + " builtin=" + isBuiltinVirtual);
            if (isGamepad && !isBuiltinVirtual) count++;
        }
        return count;
    }

    // ── Button factories ────────────────────────────────────────────────
    //
    // Three variants share the same layout + press-feedback path; only
    // the touch handler differs (button / axis / menu-toggle).

    /** Plain SDL button — DOWN sets, UP clears. */
    private static void addBtnButton(Context ctx, FrameLayout root,
                                     String label, int sdlButton,
                                     int drawableRes, int diameterDp,
                                     int gravity,
                                     int marginEndDp, int marginVerticalDp) {
        View v = buildButton(ctx, root, label, drawableRes, diameterDp,
                             gravity, marginEndDp, marginVerticalDp);
        v.setOnTouchListener((view, ev) -> {
            switch (ev.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN:
                    setButton(sdlButton, true);
                    setPressedVisuals(view, true);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL:
                    setButton(sdlButton, false);
                    setPressedVisuals(view, false);
                    return true;
                default:
                    return false;
            }
        });
    }

    /** Trigger-axis button. Press/release routes through {@link #setTrigger}
     *  so the half-range vs full-range axis convention lives in one place. */
    private static void addAxisButton(Context ctx, FrameLayout root,
                                      String label, int sdlAxis,
                                      int drawableRes, int diameterDp,
                                      int gravity,
                                      int marginEndDp, int marginVerticalDp) {
        View v = buildButton(ctx, root, label, drawableRes, diameterDp,
                             gravity, marginEndDp, marginVerticalDp);
        v.setOnTouchListener((view, ev) -> {
            switch (ev.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN:
                    setTrigger(sdlAxis, true);
                    setPressedVisuals(view, true);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL:
                    setTrigger(sdlAxis, false);
                    setPressedVisuals(view, false);
                    return true;
                default:
                    return false;
            }
        });
    }

    /** Menu-toggle button — single press → push F1 to SDL. No label. */
    private static void addMenuButton(Context ctx, FrameLayout root,
                                      int drawableRes, int diameterDp,
                                      int gravity,
                                      int marginEndDp, int marginVerticalDp) {
        View v = buildButton(ctx, root, /*label*/ null, drawableRes,
                             diameterDp, gravity, marginEndDp, marginVerticalDp);
        v.setOnTouchListener((view, ev) -> {
            switch (ev.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN:
                    setPressedVisuals(view, true);
                    return true;
                case MotionEvent.ACTION_UP:
                    toggleMenu();
                    setPressedVisuals(view, false);
                    return true;
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL:
                    setPressedVisuals(view, false);
                    return true;
                default:
                    return false;
            }
        });
    }

    /**
     * Build the visual: an ImageView with the N64-styled vector drawable
     * as background, optionally overlaid by a TextView with the label.
     * Returns the touch target (the outermost view).
     *
     * For sub-1-char labels we use a single ImageView (label-less / icon
     * baked into the vector). For multi-char labels we wrap ImageView +
     * TextView in a FrameLayout so the text floats centered above.
     */
    private static View buildButton(Context ctx, FrameLayout root,
                                    String label, int drawableRes, int diameterDp,
                                    int gravity,
                                    int marginEndDp, int marginVerticalDp) {
        int sizePx       = dp(ctx, diameterDp);
        int marginEndPx  = dp(ctx, marginEndDp);
        int marginVertPx = dp(ctx, marginVerticalDp);
        Drawable bg = ctx.getResources().getDrawable(drawableRes, ctx.getTheme());

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(sizePx, sizePx, gravity);
        if ((gravity & Gravity.BOTTOM) != 0) lp.bottomMargin = marginVertPx;
        if ((gravity & Gravity.TOP)    != 0) lp.topMargin    = marginVertPx;
        if ((gravity & Gravity.END)    != 0) lp.rightMargin  = marginEndPx;
        if ((gravity & Gravity.START)  != 0) lp.leftMargin   = marginEndPx;

        if (label == null) {
            // Icon baked into the drawable, no text overlay.
            ImageView iv = new ImageView(ctx);
            iv.setImageDrawable(bg);
            iv.setLayoutParams(lp);
            root.addView(iv);
            return iv;
        }

        // Stack the label on top of the drawable inside a FrameLayout
        // so the text floats centered without resizing the background.
        FrameLayout host = new FrameLayout(ctx);
        host.setLayoutParams(lp);

        ImageView iv = new ImageView(ctx);
        iv.setImageDrawable(bg);
        iv.setLayoutParams(new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT));
        host.addView(iv);

        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setTextColor(Color.WHITE);
        // Single chars get a bigger glyph; "Start" stays modest so it fits.
        tv.setTextSize(label.length() <= 1 ? 32f : 16f);
        // Subtle shadow keeps the label readable over any background hue.
        tv.setShadowLayer(4f, 0f, 1.5f, Color.argb(0xC0, 0, 0, 0));
        tv.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams tvLp = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT,
            Gravity.CENTER);
        tv.setLayoutParams(tvLp);
        host.addView(tv);

        root.addView(host);
        return host;
    }

    /** Visual feedback on touch: 92% scale + 70% alpha while held. */
    private static void setPressedVisuals(View v, boolean pressed) {
        if (pressed) {
            v.animate().scaleX(0.92f).scaleY(0.92f).alpha(0.7f)
                       .setDuration(40).start();
        } else {
            v.animate().scaleX(1.0f).scaleY(1.0f).alpha(1.0f)
                       .setDuration(80).start();
        }
    }

    private static int dp(Context ctx, int dp) {
        return Math.round(dp * ctx.getResources().getDisplayMetrics().density);
    }
}
