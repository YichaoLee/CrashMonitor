package monitor.ANR;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;

import monitor.MonitorUtil;

/**
 * Created by leechyli on 2017/7/14.
 */

public class ANRListener extends Thread {
    private volatile int count = 0;
    private String TAG = MonitorUtil.TAG;
    private String tracePath = "/data/anr/traces.txt";
    private final Handler handler = new Handler(Looper.getMainLooper());

    public ANRListener(String name) {
        super(name);
    }

    @Override
    public void run() {
        Log.w(TAG,"ANRListener has been registered.");
        int lastCount;
        while(true) {
            lastCount = count;
            handler.post(message);

            try {
                Thread.sleep(5000L);
            }
            catch (InterruptedException ex){
                Log.e(TAG, "Listener has been interrupted.");
                ex.printStackTrace();
                return;
            }

            if (count == lastCount) {
//                try {
//                    Thread.sleep(500L);
//                }
//                catch (InterruptedException ex){
//                    Log.e(TAG, "Listener has been interrupted.");
//                    ex.printStackTrace();
//                    return;
//                }
                handleANR();
                return;
            }
        }
    }
    private void handleANR(){
        String str = "";
        Thread mainThread = Looper.getMainLooper().getThread();
        StackTraceElement[] mainStackTrace = mainThread.getStackTrace();
        str += "Error type: ANR\n";
        for (StackTraceElement stack:mainStackTrace) {
            str += "\tat ";
            str += stack;
            str += "\n";
        }
        MonitorUtil.reportError(str, true);
//        str = readANRTrace();
//        Log.e(TAG, str);
        System.exit(0);
    }

    private String readANRTrace(){
        File file = new File(tracePath);
        BufferedReader reader = null;
        String str = "";
        try {
            Log.w(TAG, "Reading anr_trace.");
            reader = new BufferedReader(new FileReader(file));
            int line = 1;
            String tempString;
            // 一次读入一行，直到读入null为文件结束
            while ((tempString = reader.readLine()) != null) {
                // 显示行号
                str += tempString;
                str += "\n";
                line++;
            }
            reader.close();
        } catch (IOException ex) {
            Log.e(TAG, "Read /data/anr/trace.txt fail..");
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
        return str;
    }

    private final Runnable message= new Runnable() {
        @Override public void run() {
            count= (count+ 1) % 1000;
        }
    };


}
