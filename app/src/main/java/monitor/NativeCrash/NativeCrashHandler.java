package monitor.NativeCrash;
import android.content.Context;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.Set;

import monitor.MonitorUtil;
import monitor.ShellUtils;

/**
 * Created by leechyli on 2017/7/22.
 */

public class NativeCrashHandler {

    Context ctx;
    private String TAG = MonitorUtil.TAG;
    private String tombStonePath = "/data/tombstones";
    private String tombstoneFile = "/data/tombstones/tombstone_08";
    private void makeCrashReport(String reason, String threadName, int threadID) {

        Log.w(TAG, "ThreadName: "+threadName+" occurs "+reason);
        Thread t = getThreadByName(threadName);
        String stackstr = "";
        if(t!=null) {
//        RuntimeException re = new RuntimeException("crashed here (native trace should follow after the Java trace)");
            StackTraceElement[] trace = t.getStackTrace();
//        stackstr += readtombStone();
            stackstr += "java:\n";
            for (StackTraceElement element:trace) {
                stackstr += element.toString();
                stackstr += "\n";
            }
        }
        else{
            StackTraceElement[] mainStackTrace = Thread.currentThread().getStackTrace();
            stackstr += "java:\n";
            for (StackTraceElement element:mainStackTrace) {
                stackstr += element.toString();
                stackstr += "\n";
            }
        }
        Log.e("MonitorUtil", stackstr);
//        myNativeCrashHandlerToDo();
    }

    private void makeCrashReport(){
        Log.w(TAG,"12121");
//        Log.w(TAG,Log.getStackTraceString(new Throwable()));
    }

    public void myNativeCrashHandlerToDo() {
//        // 关闭应用
        unregisterForNativeCrash();
        System.exit(0);

    }

    public Thread getThreadByName(String threadName) {
        if (TextUtils.isEmpty(threadName)) {
            return null;
        }

        Set<Thread> threadSet = Thread.getAllStackTraces().keySet();
        Thread[] threadArray = threadSet.toArray(new Thread[threadSet.size()]);

        Thread theThread = null;
        for(Thread thread : threadArray) {
            Log.w(TAG, thread.getName());
            if (thread.getName() == threadName) {
                theThread =  thread;
            }
        }

        Log.d(TAG, "threadID: " + threadName + ", thread: " + theThread);
        return theThread;
    }


    public void registerForNativeCrash(Context ctx) {
        this.ctx = ctx;
        try{
            System.loadLibrary("crashMonitor");
        }
        catch (Exception ex) {
            Log.e("MonitorUtil", "Load crashMonitor.so error.");
        }
        Log.w(TAG, "threadID: "+Thread.currentThread().getId());
        if (!nRegisterForNativeCrash())
            throw new RuntimeException("Could not register for native crash as nativeCrashHandler_onLoad was not called in JNI context");
    }

    public void unregisterForNativeCrash() {
        this.ctx = null;
        nUnregisterForNativeCrash();
    }

    private native boolean nRegisterForNativeCrash();
    private native void nUnregisterForNativeCrash();

    private File getLastModifiedFile(){
        Log.w(TAG,tombStonePath+" pid:"+Thread.currentThread().getId());
        File dir = new File(tombStonePath);
        if(!dir.exists())
            Log.e(TAG, tombStonePath+" doesn't exist.");
        File[] files = dir.listFiles();
        for (File file : files) {
            Log.v(TAG, file.getName());
        }
        File latestFile = null;
        Log.w(TAG, files.length+" in "+tombStonePath);
        if(files.length>0) {
            latestFile = files[0];
            for (File file : files) {
                if (file.lastModified() > latestFile.lastModified()) {
                    latestFile = file;
                }
            }
        }
        else
            Log.e(TAG, "No files in "+tombStonePath);
        Log.w(TAG,"latest file is " + latestFile);
        return latestFile;
    }


    private String readtombStone(){

        File file = new File(tombstoneFile);//getLastModifiedFile();
        String str = "";
        if(file!=null) {
            BufferedReader reader = null;
            try {
                Log.w(TAG, "Reading tombStone.");
                reader = new BufferedReader(new FileReader(file));
                int line = 1;
                String tempString;
                // 一次读入一行，直到读入null为文件结束
                while ((tempString = reader.readLine()) != null) {
                    // 显示行号
                    if (tempString.startsWith("#"))
                        str += tempString;
                    line++;
                }
                reader.close();
            } catch (IOException ex) {
                Log.e(TAG, "Read "+file.getName()+" failed.");
                ex.printStackTrace();
            } finally {
                if (reader != null) {
                    try {
                        reader.close();
                    } catch (IOException e1) {
                        Log.e(TAG, "Read fail, close the file.");
                    }
                }
            }
        }
        return str;
    }

    class CrashHandlerThread extends Thread{
        public void run(){

        }
    }

}