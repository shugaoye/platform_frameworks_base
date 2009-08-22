package android.net.ethernet;

import java.net.InetAddress;
import java.net.UnknownHostException;

import android.bluetooth.BluetoothHeadset;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.DhcpInfo;
import android.net.NetworkStateTracker;
import android.net.NetworkUtils;
import android.net.NetworkInfo.DetailedState;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.os.Parcel;
import android.util.*;

public class EthernetStateTracker extends NetworkStateTracker {
	private static final String TAG="EtherenetStateTracker";
	private static final int EVENT_DHCP_START                        = 0;
	private static final int EVENT_INTERFACE_CONFIGURATION_SUCCEEDED = 1;
	private static final int EVENT_INTERFACE_CONFIGURATION_FAILED    = 2;

	private EthernetManager mEM;
	private boolean mServiceStarted;
	private boolean mIfEnabledPending;
	private DhcpHandler mDhcpTarget;
	private String mInterfaceName ;
	private DhcpInfo mDhcpInfo;
	private EthernetMonitor mMonitor;
	private String[] sDnsPropNames;

	public EthernetStateTracker(Context context, Handler target) {

		super(context, target, ConnectivityManager.TYPE_ETH, 0, "ETH", "");
		Log.i(TAG,"Starts...");


		if(EthernetNative.initEthernetNative() != 0 )
		{
			Log.e(TAG,"Can not init etherent device layers");
			return;
		}
		Log.i(TAG,"Successed");
		mServiceStarted = true;
		HandlerThread dhcpThread = new HandlerThread("DHCP Handler Thread");
        dhcpThread.start();
        mDhcpTarget = new DhcpHandler(dhcpThread.getLooper(), this);
        mMonitor = new EthernetMonitor(this);
        mDhcpInfo = new DhcpInfo();
	}

	public boolean stopInteface() {
		EthernetDevInfo info = mEM.getSavedEthConfig();
		if (info != null && mEM.ethConfigured())
		{
			mDhcpTarget.removeMessages(EVENT_DHCP_START);
			String ifname = info.getIfName();
			NetworkUtils.resetConnections(ifname);
			NetworkUtils.disableInterface(ifname);
		}

		return true;
	}

	private static int stringToIpAddr(String addrString) throws UnknownHostException {
        try {
		if (addrString == null)
			return 0;
            String[] parts = addrString.split("\\.");
            if (parts.length != 4) {
                throw new UnknownHostException(addrString);
            }

            int a = Integer.parseInt(parts[0])      ;
            int b = Integer.parseInt(parts[1]) <<  8;
            int c = Integer.parseInt(parts[2]) << 16;
            int d = Integer.parseInt(parts[3]) << 24;

            return a | b | c | d;
        } catch (NumberFormatException ex) {
            throw new UnknownHostException(addrString);
        }
    }

	public boolean configureInterface(EthernetDevInfo info) throws UnknownHostException {

	        if (info.getConnectMode().equals(EthernetDevInfo.ETH_CONN_MODE_DHCP)) {
			Log.i(TAG, "trigger dhcp for device " + info.getIfName());
	            if (!mIfEnabledPending) {
			mIfEnabledPending = true;
			Log.i(TAG, "pass message to DHCP");
	                mDhcpTarget.sendEmptyMessage(EVENT_DHCP_START);
	            }
	        } else {
	            int event;
	            Log.i(TAG, "set ip manually");
	            mDhcpInfo.ipAddress =  stringToIpAddr(info.getIpAddress());
	            mDhcpInfo.gateway = stringToIpAddr(info.getRouteAddr());
	            mDhcpInfo.netmask = stringToIpAddr(info.getNetMask());
	            mDhcpInfo.dns1 = stringToIpAddr(info.getDnsAddr());
	            mDhcpInfo.dns2 = 0;
	            mIfEnabledPending = true;
	            if (NetworkUtils.configureInterface(info.getIfName(), mDhcpInfo)) {
	                event = EVENT_INTERFACE_CONFIGURATION_SUCCEEDED;
	                Log.v(TAG, "Static IP configuration succeeded");
	            } else {
	                event = EVENT_INTERFACE_CONFIGURATION_FAILED;
	                Log.v(TAG, "Static IP configuration failed");
	            }
	            sendEmptyMessage(event);
	        }
			return true;
	}

	public boolean waitingForConnectUpdate()
	{
		return mIfEnabledPending;
	}

	public void ClearConntectUpdateWating()
	{
		mIfEnabledPending = false;
	}


	public boolean resetInterface()  throws UnknownHostException{
		/*
		 * This will guide us to enabled the enabled device
		 */
		if (mEM != null) {
			EthernetDevInfo info = mEM.getSavedEthConfig();
			if (info != null && mEM.ethConfigured())
			{

				mInterfaceName = info.getIfName();
				sDnsPropNames = new String[] {
	                    "dhcp." + mInterfaceName + ".dns1",
	                    "dhcp." + mInterfaceName + ".dns2"
	                };
				Log.i(TAG, "reset device " + mInterfaceName);
				NetworkUtils.resetConnections(mInterfaceName);
				 // Stop DHCP
		        if (mDhcpTarget != null) {
		            mDhcpTarget.removeMessages(EVENT_DHCP_START);
		        }
		        if (!NetworkUtils.stopDhcp(mInterfaceName)) {
		            Log.e(TAG, "Could not stop DHCP");
		        }
		        configureInterface(info);
			}
		}
		return true;
	}

	@Override
	public String[] getNameServers() {
		Log.i(TAG,"get dns from " + sDnsPropNames);
		 return getNameServerList(sDnsPropNames);
	}

	@Override
	public String getTcpBufferSizesPropName() {
		// TODO Auto-generated method stub
		return "net.tcp.buffersize.default";
	}

	public void StartPolling() {
		Log.i(TAG, "start polling");
		mMonitor.startMonitoring();
	}
	@Override
	public boolean isAvailable() {
		return (mEM.getTotalInterface() != 0);
	}

	@Override
	public boolean reconnect() {
		try {
			return resetInterface();
		} catch (UnknownHostException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		return false;

	}

	@Override
	public boolean setRadio(boolean turnOn) {
		// TODO Auto-generated method stub
		return false;
	}

	@Override
	public void startMonitoring() {
		Log.i(TAG,"start to monitor the ethernet devices");
		if (mServiceStarted )	{
			mEM = (EthernetManager)mContext.getSystemService(Context.ETH_SERVICE);
			if (mEM.getEthState()==mEM.ETH_STATE_ENABLED) {
				try {
					resetInterface();
				} catch (UnknownHostException e) {
					Log.e(TAG, "Wrong ethernet configuration");
				}
			}
		}

	}

	@Override
	public int startUsingNetworkFeature(String feature, int callingPid,
			int callingUid) {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public int stopUsingNetworkFeature(String feature, int callingPid,
			int callingUid) {
		// TODO Auto-generated method stub
		return 0;
	}

	@Override
	public boolean teardown() {
		return stopInteface();

	}

	public void handleMessage(Message msg) {
        switch (msg.what) {
        case EVENT_INTERFACE_CONFIGURATION_SUCCEEDED:
		break;
        case EVENT_INTERFACE_CONFIGURATION_FAILED:
		//start to retry ?

		break;
        }
	}

	private class DhcpHandler extends Handler {
		private Handler mTrackerTarget;

		 public DhcpHandler(Looper looper, Handler target) {
	            super(looper);
	            mTrackerTarget = target;

	        }

		  public void handleMessage(Message msg) {
	            int event;

	            switch (msg.what) {
	                case EVENT_DHCP_START:
				  Log.d(TAG, "DhcpHandler: DHCP request started");
	                      if (NetworkUtils.runDhcp(mInterfaceName, mDhcpInfo)) {
	                          event = EVENT_INTERFACE_CONFIGURATION_SUCCEEDED;
	                           Log.v(TAG, "DhcpHandler: DHCP request succeeded");
	                      } else {
	                          event = EVENT_INTERFACE_CONFIGURATION_FAILED;
	                          mIfEnabledPending = false;
	                          Log.i(TAG, "DhcpHandler: DHCP request failed: " +
	                              NetworkUtils.getDhcpError());
	                      }
	                      mTrackerTarget.sendEmptyMessage(event);
	                      break;
	            }

		  }
	}

	public void notifyStateChange(String ifname,DetailedState state) {
		Log.i(TAG, "report new state " + state.toString() + " on dev " + ifname);
		if (ifname.equals(mInterfaceName)) {
			Log.i(TAG, "update network state tracker");
			setDetailedState(state);
			mTarget.sendEmptyMessage(EVENT_CONFIGURATION_CHANGED);

		}
	}
}
