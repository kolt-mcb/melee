package org.libsdl.app;

import android.os.Build;
import android.view.Display;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * SDLActivity with the system bars actually hidden.
 *
 * SDL asks for immersive mode from native code: creating the window with
 * SDL_WINDOW_FULLSCREEN_DESKTOP (window.c does, on Android) reaches
 * SDLActivity.setWindowStyle(true), which sets the legacy
 * View.SYSTEM_UI_FLAG_FULLSCREEN | HIDE_NAVIGATION | IMMERSIVE_STICKY.
 *
 * On Android 16 that legacy path only half works. Measured on a Pixel 9: the
 * navigation bar goes (dumpsys: navigationBars visible=false), but the status
 * bar comes back as a transient bar and then stays -- dumpsys reports
 * mShowingTransientInsetsTypes=statusBars with nothing having touched the
 * screen, and it is drawn over the top of the game for as long as it runs.
 *
 * WindowInsetsController (API 30+) is the supported replacement and does hide
 * it. BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE keeps what IMMERSIVE_STICKY gave:
 * a swipe from the edge reveals the bars briefly, then they go again.
 *
 * Applied on every focus gain, not just at startup, because the bars come
 * back with the activity -- after a notification shade pull, a task switch,
 * or the screen locking and unlocking.
 */
public class MeleeActivity extends SDLActivity {

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT < 30 /* Android 11 (R) */) {
            return;
        }
        WindowInsetsController c = getWindow().getInsetsController();
        if (c == null) {
            return;
        }
        c.setSystemBarsBehavior(
            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        c.hide(WindowInsets.Type.systemBars());
    }

    /**
     * Ask the display to sit at 60 Hz.
     *
     * The game is a fixed-step 60 Hz simulation, so 60 Hz is the rate it
     * actually wants: the swap then paces it exactly, with no second clock to
     * beat against and no panel running twice as fast as the content.
     *
     * Left alone, a Pixel 9 moves between 120 Hz, 60 Hz and 30 Hz as it sees
     * fit (the display reports FLAG_ALLOWS_CONTENT_MODE_SWITCH and offers
     * both modes). The swap interval is picked from the rate the panel
     * happened to have at startup -- 2, for 120 Hz -- and one present every
     * two refreshes of a 60 Hz panel is 30 fps, or 15 on a 30 Hz one. It
     * comes and goes with Android's choice, which is what "a lot of lag
     * sometimes" describes.
     *
     * A preferred rate is a request, not a guarantee, so the native side
     * re-checks the real rate every second and re-picks the interval
     * (window_sync_swap_interval).
     */
    private void preferSixtyHz() {
        Display d = getDisplay();
        if (d == null) {
            return;
        }
        Display.Mode cur = d.getMode();
        float want = 0.0f;
        for (Display.Mode m : d.getSupportedModes()) {
            if (m.getPhysicalWidth() != cur.getPhysicalWidth() ||
                m.getPhysicalHeight() != cur.getPhysicalHeight())
            {
                continue;
            }
            float r = m.getRefreshRate();
            /* 60 exactly if it is offered, else the fastest on offer -- the
             * native side will divide that down to 60. */
            if (r > 59.0f && r < 61.0f) {
                want = r;
                break;
            }
            if (r > want) {
                want = r;
            }
        }
        if (want > 0.0f) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredRefreshRate = want;
            getWindow().setAttributes(lp);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
            preferSixtyHz();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
    }
}
