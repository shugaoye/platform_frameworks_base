package com.android.commands.svc;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.IConnectivityManager;
import android.net.ethernet.IEthernetManager;
import android.net.ethernet.EthernetManager;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;

public class EthernetCommand extends Svc.Command{
	public static final String TAG = "Ethernet_SVC";
	public EthernetCommand() {
		super("ethernet");
		// TODO Auto-generated constructor stub
	}
	public String shortHelp() {
	    return "Control the Ethernet manager";
	}

	public String longHelp() {
	    return shortHelp() + "\n"
	            + "\n"
	            + "usage: svc ethernet [enable|disable]\n"
	            + "         Turn Ethernet on or off.\n\n"
	            + "       svc wifi prefer\n"
	            + "          Set Ethernet as the preferred data network\n";
	}

	public void run(String[] args) {
		Log.i(TAG, "kick off etherent" + args);
	    boolean validCommand = false;
	    if (args.length >= 2) {
	        boolean flag = false;
	        if ("enable".equals(args[1])) {
	                flag = true;
	                validCommand = true;
	            } else if ("disable".equals(args[1])) {
	                flag = false;
	                validCommand = true;
	            } else if ("prefer".equals(args[1])) {
	                IConnectivityManager connMgr =
	                        IConnectivityManager.Stub.asInterface(ServiceManager.getService(Context.CONNECTIVITY_SERVICE));
	                try {
	                    connMgr.setNetworkPreference(ConnectivityManager.TYPE_ETH);
	                } catch (RemoteException e) {
	                    System.err.println("Failed to set preferred network: " + e);
	                }
	                return;
	            }
	            if (validCommand) {
	                IEthernetManager ethMgr
	                        = IEthernetManager.Stub.asInterface(ServiceManager.getService(Context.ETH_SERVICE));
	                try {
	                    ethMgr.setEthState(flag ? EthernetManager.ETH_STATE_ENABLED:EthernetManager.ETH_STATE_DISABLED);
	                }
	                catch (RemoteException e) {
	                    System.err.println("Wi-Fi operation failed: " + e);
	                }
	                return;
	            }
	        }
	        System.err.println(longHelp());
	    }
}
