package com.android.server;

import java.net.UnknownHostException;
import android.net.ethernet.EthernetNative;
import android.net.ethernet.IEthernetManager;
import android.net.ethernet.EthernetManager;
import android.net.ethernet.EthernetStateTracker;
import android.net.ethernet.EthernetDevInfo;
import android.provider.Settings;
import android.util.Log;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;

public class EthernetService<syncronized> extends IEthernetManager.Stub{
	private Context mContext;
	private EthernetStateTracker mTracker;
	private String[] DevName;
	private static final String TAG = "EthernetService";
	private boolean isEthEnabled ;
	private int mEthState= EthernetManager.ETH_STATE_DISABLED;


	public EthernetService(Context context, EthernetStateTracker Tracker){
		mTracker = Tracker;
		mContext = context;

		isEthEnabled = getPersistedEthEnabled();
		Log.i(TAG,"Ethernet dev enabled " + isEthEnabled );
		setEthState(isEthEnabled ? EthernetManager.ETH_STATE_ENABLED:EthernetManager.ETH_STATE_DISABLED);
		Log.i(TAG, "Trigger the ethernet monitor");
		mTracker.StartPolling();
	}

	public boolean isEthConfigured() {

		final ContentResolver cr = mContext.getContentResolver();
	    int x = Settings.Secure.getInt(cr, Settings.Secure.ETH_CONF,0);

	    if (x == 1)
		return true;
	    return false;
	}

	public synchronized EthernetDevInfo getSavedEthConfig() {

		if (isEthConfigured() ) {
			final ContentResolver cr = mContext.getContentResolver();
			EthernetDevInfo info = new EthernetDevInfo();
			info.setConnectMode(Settings.Secure.getString(cr, Settings.Secure.ETH_MODE));
			info.setIfName(Settings.Secure.getString(cr, Settings.Secure.ETH_IFNAME));
			info.setIpAddress(Settings.Secure.getString(cr, Settings.Secure.ETH_IP));
			info.setDnsAddr(Settings.Secure.getString(cr, Settings.Secure.ETH_DNS));
			info.setNetMask(Settings.Secure.getString(cr, Settings.Secure.ETH_MASK));
			info.setRouteAddr(Settings.Secure.getString(cr, Settings.Secure.ETH_ROUTE));

			return info;
		}
		return null;
	}


	public synchronized void UpdateEthDevInfo(EthernetDevInfo info) {
		final ContentResolver cr = mContext.getContentResolver();
	    Settings.Secure.putInt(cr, Settings.Secure.ETH_CONF,1);
	    Settings.Secure.putString(cr, Settings.Secure.ETH_IFNAME, info.getIfName());
	    Settings.Secure.putString(cr, Settings.Secure.ETH_IP, info.getIpAddress());
	    Settings.Secure.putString(cr, Settings.Secure.ETH_MODE, info.getConnectMode());
	    Settings.Secure.putString(cr, Settings.Secure.ETH_DNS, info.getDnsAddr());
	    Settings.Secure.putString(cr, Settings.Secure.ETH_ROUTE, info.getRouteAddr());
	    Settings.Secure.putString(cr, Settings.Secure.ETH_MASK,info.getNetMask());
	    if (mEthState == EthernetManager.ETH_STATE_ENABLED) {
		try {
				mTracker.resetInterface();
			} catch (UnknownHostException e) {
				Log.e(TAG, "Wrong ethernet configuration");
			}

	    }

	}

	public int getTotalInterface() {
		return EthernetNative.getInterfaceCnt();
	}


	private int scanEthDevice() {
		int i = 0,j;
		if ((i = EthernetNative.getInterfaceCnt()) != 0) {
			Log.i(TAG, "total found "+i+ " net devices");
			DevName = new String[i];
		}
		else
			return i;

		for (j = 0; j < i; j++) {
			DevName[j] = EthernetNative.getInterfaceName(j);
			if (DevName[j] == null)
				break;
			Log.i(TAG,"device " + j + " name " + DevName[j]);
		}

		return i;
	}

	public String[] getDeviceNameList() {
		if (scanEthDevice() > 0 )
			return DevName;
		else
			return null;
	}

	private boolean getPersistedEthEnabled() {
		final ContentResolver cr = mContext.getContentResolver();
	    try {
	        return Settings.Secure.getInt(cr, Settings.Secure.ETH_ON) == 1;
	    } catch (Settings.SettingNotFoundException e) {
	        Settings.Secure.putInt(cr, Settings.Secure.ETH_ON, 0);
	        return false;
	    }
    }

	private synchronized void persistEthEnabled(boolean enabled) {
	    final ContentResolver cr = mContext.getContentResolver();
	    Settings.Secure.putInt(cr, Settings.Secure.ETH_ON, enabled ? 1 : 0);
	}

	public synchronized void setEthState(int state) {
		Log.i(TAG, "setEthState from " + mEthState + " to "+ state);

		if (mEthState != state){
			mEthState = state;
			if (state == EthernetManager.ETH_STATE_DISABLED) {
				persistEthEnabled(false);
				mTracker.stopInteface();
			} else {
				persistEthEnabled(true);
				try {
					mTracker.resetInterface();
				} catch (UnknownHostException e) {
					Log.e(TAG, "Wrong ethernet configuration");
				}

			}
		}
	}

	public int getEthState( ) {
		return mEthState;
	}
}
