package com.nimbus.engine;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;

/** Android host supporting both Nimbus Engine Runtime and Nimbus Studio Editor flavors. */
public final class MainActivity extends Activity implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("nimbus_android");
    }

    private SurfaceView surfaceView;
    private TextView statusView;
    private boolean resumed;
    private boolean runtimeStarted;
    private boolean pausedState;
    private int lastSurfaceWidth;
    private int lastSurfaceHeight;
    private float lastTouchX;
    private float lastTouchY;

    private native boolean nativeStart(Surface surface, int width, int height);
    private native void nativeStop();
    private native void nativeTouch(int pointerId, int action, float x, float y, float dx, float dy);
    private native void nativeSetPaused(boolean paused);
    private native void nativePlayTone(float hz);
    private native int nativeCreateEntity(String name);
    private native boolean nativeSaveProject(String path);
    private native int nativeEntityCount();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(15, 17, 21));

        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        surfaceView.setOnTouchListener((v, event) -> {
            int action = event.getActionMasked();
            int idx = event.getActionIndex();
            int pid = event.getPointerId(idx);
            float x = event.getX(idx);
            float y = event.getY(idx);
            float dx = (action == MotionEvent.ACTION_MOVE) ? (x - lastTouchX) : 0f;
            float dy = (action == MotionEvent.ACTION_MOVE) ? (y - lastTouchY) : 0f;
            lastTouchX = x;
            lastTouchY = y;
            int nativeAction = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) ? 0
                    : (action == MotionEvent.ACTION_MOVE ? 2 : 1);
            if (runtimeStarted) {
                nativeTouch(pid, nativeAction, x, y, dx, dy);
            }
            return true;
        });
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.VERTICAL);
        header.setPadding(dp(18), dp(14), dp(18), dp(10));
        header.setBackgroundColor(0xCC171A21);
        TextView brand = new TextView(this);
        brand.setText(BuildConfig.IS_EDITOR ? "NIMBUS STUDIO" : "NIMBUS ENGINE");
        brand.setTextColor(Color.rgb(56, 189, 248));
        brand.setTextSize(14);
        brand.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        TextView subtitle = new TextView(this);
        subtitle.setText(BuildConfig.IS_EDITOR
                ? "MOBILE EDITOR  ·  C++20  ·  OPENGLES 3  ·  TOUCH WORKBENCH"
                : "ANDROID RUNTIME  ·  C++20  ·  OPENGLES 3");
        subtitle.setTextColor(Color.LTGRAY);
        subtitle.setTextSize(10);
        subtitle.setPadding(0, dp(3), 0, 0);
        header.addView(brand);
        header.addView(subtitle);
        root.addView(header, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP));

        LinearLayout footer = new LinearLayout(this);
        footer.setGravity(Gravity.CENTER_VERTICAL);
        footer.setOrientation(LinearLayout.HORIZONTAL);
        footer.setPadding(dp(14), dp(8), dp(14), dp(8));
        footer.setBackgroundColor(0xD9171A21);
        statusView = new TextView(this);
        statusView.setText("Waiting for Android surface…");
        statusView.setTextColor(Color.WHITE);
        statusView.setTextSize(11);
        footer.addView(statusView, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        Button pauseBtn = new Button(this);
        pauseBtn.setText("Pause");
        pauseBtn.setAllCaps(false);
        pauseBtn.setOnClickListener(v -> {
            pausedState = !pausedState;
            nativeSetPaused(pausedState);
            pauseBtn.setText(pausedState ? "Play" : "Pause");
        });
        footer.addView(pauseBtn);

        Button toneBtn = new Button(this);
        toneBtn.setText("Tone");
        toneBtn.setAllCaps(false);
        toneBtn.setOnClickListener(v -> nativePlayTone(440f));
        footer.addView(toneBtn);

        if (BuildConfig.IS_EDITOR) {
            Button addBtn = new Button(this);
            addBtn.setText("+Entity");
            addBtn.setAllCaps(false);
            addBtn.setOnClickListener(v -> {
                int eid = nativeCreateEntity("MobileEntity");
                statusView.setText("Created #" + eid + " · Total entities: " + nativeEntityCount());
            });
            footer.addView(addBtn);

            Button saveBtn = new Button(this);
            saveBtn.setText("Save");
            saveBtn.setAllCaps(false);
            saveBtn.setOnClickListener(v -> {
                File dir = new File(getFilesDir(), "nimbus_project");
                boolean ok = nativeSaveProject(dir.getAbsolutePath());
                statusView.setText(ok ? "Saved project to app storage" : "Project save unavailable");
            });
            footer.addView(saveBtn);
        }

        root.addView(footer, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM));

        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        startRuntimeIfReady();
    }

    @Override
    protected void onPause() {
        resumed = false;
        stopRuntime();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        stopRuntime();
        super.onDestroy();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        statusView.setText("Surface ready — starting engine…");
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (width != lastSurfaceWidth || height != lastSurfaceHeight) {
            stopRuntime();
            lastSurfaceWidth = width;
            lastSurfaceHeight = height;
        }
        startRuntimeIfReady();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        stopRuntime();
        statusView.setText("Surface paused");
    }

    private void startRuntimeIfReady() {
        if (!resumed || surfaceView == null || runtimeStarted) return;
        SurfaceHolder holder = surfaceView.getHolder();
        Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) return;
        int width = surfaceView.getWidth();
        int height = surfaceView.getHeight();
        if (width <= 0 || height <= 0) return;

        runtimeStarted = nativeStart(surface, width, height);
        statusView.setText(runtimeStarted
                ? ("Active · Entities: " + nativeEntityCount() + " · Drag to orbit")
                : "Runtime startup failed · check Logcat tag NimbusEngine");
    }

    private void stopRuntime() {
        if (!runtimeStarted) return;
        nativeStop();
        runtimeStarted = false;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
