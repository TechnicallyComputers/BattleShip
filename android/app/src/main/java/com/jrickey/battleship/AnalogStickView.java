package com.jrickey.battleship;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.MotionEvent;
import android.view.View;

/**
 * Fixed-anchor virtual analog stick. The ring stays in the same place inside
 * its hosting region; touch delta from that anchor sets
 * SDL_CONTROLLER_AXIS_LEFTX/Y. On release, the axes return to centered.
 */
public final class AnalogStickView extends View {

    /* SDL_GameControllerAxis mirrors. */
    private static final int SDL_AXIS_LEFTX = 0;
    private static final int SDL_AXIS_LEFTY = 1;

    /** Pixels of drag distance that map to full deflection on the SDL axis. */
    private final int mDeflectionPx;

    /** Visual radius for the outer ring. */
    private final int mRingPx;
    /** Visual radius for the moving thumb. */
    private final int mThumbPx;

    private final Paint mRingPaint;
    private final Paint mThumbPaint;

    /** Active touch pointer ID, or -1 when idle. */
    private int  mActivePointerId = -1;
    /** Stable anchor inside the left-stick region. */
    private float mAnchorX, mAnchorY;
    /** Clamped thumb position. */
    private float mTouchX, mTouchY;
    private boolean mAnchorInitialized = false;

    public AnalogStickView(Context ctx) {
        super(ctx);
        float density = ctx.getResources().getDisplayMetrics().density;
        mDeflectionPx = Math.round(80f * density);
        mRingPx       = Math.round(70f * density);
        mThumbPx      = Math.round(34f * density);

        mRingPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mRingPaint.setStyle(Paint.Style.STROKE);
        mRingPaint.setStrokeWidth(Math.max(2f, 2f * density));
        mRingPaint.setColor(Color.argb(0xC0, 0xFF, 0xFF, 0xFF));

        mThumbPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mThumbPaint.setColor(Color.argb(0xA0, 0x33, 0x99, 0xFF));
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (w <= 0 || h <= 0) {
            return;
        }

        if (mAnchorInitialized && oldw > 0 && oldh > 0) {
            mAnchorX = Math.max(0f, Math.min(w, mAnchorX * w / oldw));
            mAnchorY = Math.max(0f, Math.min(h, mAnchorY * h / oldh));
        } else {
            mAnchorX = w * 0.5f;
            mAnchorY = h * 0.5f;
            mAnchorInitialized = true;
        }

        if (mActivePointerId < 0) {
            centerThumb();
        }
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (!mAnchorInitialized) {
            return;
        }
        canvas.drawCircle(mAnchorX, mAnchorY, mRingPx,  mRingPaint);
        canvas.drawCircle(mTouchX,  mTouchY,  mThumbPx, mThumbPaint);
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        switch (ev.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (mActivePointerId >= 0) {
                    // Already tracking a pointer in this view — ignore others.
                    return true;
                }
                int idx = ev.getActionIndex();
                ensureAnchor();
                mActivePointerId = ev.getPointerId(idx);
                updateTouch(ev.getX(idx), ev.getY(idx));
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                int idx = ev.findPointerIndex(mActivePointerId);
                if (idx < 0) return true;
                updateTouch(ev.getX(idx), ev.getY(idx));
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                // CANCEL applies to the whole gesture and getActionIndex()
                // is always 0 here — if we were tracking a secondary
                // pointer, an ID check would skip the reset and leave the
                // stick stuck at its last deflection. Reset unconditionally.
                mActivePointerId = -1;
                resetInput();
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int upId = ev.getPointerId(ev.getActionIndex());
                if (upId != mActivePointerId) {
                    return true;
                }
                mActivePointerId = -1;
                resetInput();
                invalidate();
                return true;
            }
            default:
                return false;
        }
    }

    private void ensureAnchor() {
        if (mAnchorInitialized) {
            return;
        }
        int w = getWidth();
        int h = getHeight();
        mAnchorX = w > 0 ? w * 0.5f : 0f;
        mAnchorY = h > 0 ? h * 0.5f : 0f;
        mAnchorInitialized = true;
        centerThumb();
    }

    private void updateTouch(float x, float y) {
        float dx = x - mAnchorX;
        float dy = y - mAnchorY;
        float len = (float) Math.hypot(dx, dy);
        float clampedDx = dx, clampedDy = dy;
        if (len > mDeflectionPx) {
            clampedDx = dx * mDeflectionPx / len;
            clampedDy = dy * mDeflectionPx / len;
        }
        mTouchX = mAnchorX + clampedDx;
        mTouchY = mAnchorY + clampedDy;
        emitAxis(clampedDx / mDeflectionPx,
                 clampedDy / mDeflectionPx);
    }

    private void resetInput() {
        centerThumb();
        emitAxis(0f, 0f);
    }

    private void centerThumb() {
        mTouchX = mAnchorX;
        mTouchY = mAnchorY;
    }

    /** @param nx,ny normalized [-1, 1]; positive Y is downward (matches SDL). */
    private void emitAxis(float nx, float ny) {
        // SDL signed-16 axis range
        int sx = (int) Math.round(Math.max(-1f, Math.min(1f, nx)) * 32767f);
        int sy = (int) Math.round(Math.max(-1f, Math.min(1f, ny)) * 32767f);
        TouchOverlay.setAxis(SDL_AXIS_LEFTX, sx);
        TouchOverlay.setAxis(SDL_AXIS_LEFTY, sy);
    }
}
