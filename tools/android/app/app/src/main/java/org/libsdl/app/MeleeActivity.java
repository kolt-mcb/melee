package org.libsdl.app;

import android.os.Build;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

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

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
    }
}
