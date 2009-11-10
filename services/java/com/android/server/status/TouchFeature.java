/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server.status;

import android.app.StatusBarManager;
import android.content.Context;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;
import android.view.Display;
import android.view.IWindowManager;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;
import android.widget.Toast;

public class TouchFeature {
	private String TAG = "TouchFeature";
	private int mX1 = 0;
	private int mX2 = 0;
	private int mScreenWidth = 1024;
	private boolean mDisableExpand = false;
	private int mKey = KeyEvent.KEYCODE_HOME;
	private IBinder mToken;
	private Handler mHandler;
	private Context mContext;
	private StatusBarService mService;
	private final Display mDisplay;

	protected TouchFeature(Context context){
		mContext = context;
		mToken = new Binder();
		mHandler = new Handler();
		mDisplay = ((WindowManager)context.getSystemService(
			Context.WINDOW_SERVICE)).getDefaultDisplay();
		mScreenWidth = mDisplay.getWidth();
	}

	// ================================================================================
	// Add to do key "menu" "home" "back" function
	// Touch the right corner of the screen on the statusBar to enable/disable the function
	// Menu: Click/Touch the statusBar
	// Home: Touch the statusBar from left to right
	// Back: Touch the statusBar from right to left
	// ================================================================================
	protected void adjust(StatusBarService service, MotionEvent event){
		mService = service;

		if (event.getAction() == MotionEvent.ACTION_DOWN){
			mX1 = (int)event.getRawX();

			// when touch the right corner of the screen, enable/disable the expand
			if (mX1 > (mScreenWidth - 50)) {
				if (mDisableExpand == true){
					enableExpand();
				}
				else {
					disableExpand();
				}
				return;
			}
		}

		if (event.getAction() == MotionEvent.ACTION_UP){
			mX2 = (int)event.getRawX();

			// if the corner touched, when up, nothing to do
			if (mX1 > (mScreenWidth - 50))
				return;

			if (mX2 > mX1) {
				runKey(KeyEvent.KEYCODE_MENU);
			}
			else if (mX2 < mX1) {
				runKey(KeyEvent.KEYCODE_BACK);
			}
			else
				runKey(KeyEvent.KEYCODE_HOME);
		}
	}

	// to disable the statusbar expanding
	private void disableExpand(){
		Log.v(TAG, "disable expand the statusbar");
		Toast.makeText(mContext, "Please wait for enable the function to do menu on statusBar", Toast.LENGTH_SHORT).show();
		mService.disable(StatusBarManager.DISABLE_EXPAND, mToken, mContext.getPackageName());
		Toast.makeText(mContext, "You can touch the statusBar to do function of menu now", Toast.LENGTH_SHORT).show();
		mDisableExpand = true;
	}

	// to enable the statusbar expanding
	private void enableExpand(){
		Log.v(TAG, "enable expand the statusbar");
		Toast.makeText(mContext, "Please wait for enable the function of notification", Toast.LENGTH_SHORT).show();
		mService.disable(0, mToken, mContext.getPackageName());
		Toast.makeText(mContext, "You can touch the statusBar normally", Toast.LENGTH_SHORT).show();
		mDisableExpand = false;
	}

	// dispatch the keyEvent to the view
	private void runKey(int key) {
		if (mDisableExpand == false) {
			return;
		}
		mKey = key;
		mHandler.post(new Runnable() {
			public void run() {
				press(mKey);
			}
		});
	}

	// simulate the keyEvent
	public void press(int key) {
	    sendKey(new KeyEvent(KeyEvent.ACTION_DOWN, key));
	    sendKey(new KeyEvent(KeyEvent.ACTION_UP, key));
	}

	public void sendKey(KeyEvent event) {
	    try {
	        (IWindowManager.Stub.asInterface(ServiceManager.getService("window")))
	            .injectKeyEvent(event, true);
	    } catch (RemoteException e) {
	    }
	}
}
