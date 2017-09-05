package monitor.NativeCrash;

import android.app.Activity;
import android.app.ProgressDialog;
import android.os.AsyncTask;
import android.os.Bundle;
import android.support.v7.app.AlertDialog;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

import com.example.leechyli.crashmonitor.R;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.text.MessageFormat;

import monitor.MonitorUtil;

/**
 * Created by leechyli on 2017/7/25.
 */

public class CrashHandler extends Activity {

    private String TAG = "MonitorUtil";
    private String tombStonePath = "/data/tombstones";
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);


//        NativeError e = (NativeError)getIntent().getSerializableExtra("error");
        RuntimeException re = (RuntimeException)getIntent().getSerializableExtra("error");

        StackTraceElement[] trace = re.getStackTrace();
        String stackstr = "";
//        stackstr += readtombStone();
        stackstr += "java:\n";
        for (int i=0;i<trace.length;++i) {
            StackTraceElement element = trace[i];
            stackstr += element.toString();
            stackstr += "\n";
        }
        Log.e("MonitorUtil",stackstr);

//        throw re;
        super.onDestroy();
    }

    private File getLastModifiedFile(){
        File dir = new File(tombStonePath);
        File[] files = dir.listFiles();
        File latestFile = null;
        if(files.length>0) {
            latestFile = files[0];
            for (File file : files) {
                if (file.lastModified() > latestFile.lastModified()) {
                    latestFile = file;
                }
            }
        }
        Log.w(TAG,"latest file is " + latestFile);
        return latestFile;
    }


    private String readtombStone(){

        File file = getLastModifiedFile();
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

}

