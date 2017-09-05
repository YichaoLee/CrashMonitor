package monitor;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;
import android.os.Looper;
import android.util.Log;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.io.Writer;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;

import monitor.ANR.ANRListener;
import monitor.JavaCrash.JavaCrashHandler;
import monitor.NativeCrash.NativeCrashHandler;
import monitor.NativeCrash.NativeError;


/**
 * Created by leechyli on 2017/7/14.
 */
public class MonitorUtil {

    public static final String TAG = "CrashMonitor";

    /** 保存app Context */
    private static Context applicationContext;
    /** 设置写入文件开关*/
    private static Boolean MYLOG_WRITE_TO_FILE=true;
    /** 日志文件在sdcard中的路径 */
    private static String MYLOG_PATH_SDCARD_DIR="/sdcard/";
    /** 本类输出的日志文件名称 */
    private static String MYLOGFILEName = "CrashMonitorLog.txt";
    private static String logText;
    /** 日志文件格式 */
    private static SimpleDateFormat logfile = new SimpleDateFormat("yyyy-MM-dd");

    /**
     * 初始化Context，为线程添加CrashHandler,ANRListener,NativeCrashHandler
     * */
    public static void initial(Context context, boolean writeToFile) {
        applicationContext = context;
        MYLOG_WRITE_TO_FILE = writeToFile;
        new NativeCrashHandler().registerForNativeCrash(context);
        JavaCrashHandler.registerJavaCrashHandler(context);
        new ANRListener("ANRListener").start();
    }

    public static Context getContext() {
        return applicationContext;
    }

    public static void monitorThis(Object arg1, boolean arg2) {
        if (arg1 != null){
            Log.w(TAG, arg1.toString() + "\t" + Boolean.toString(arg2));
        }
    }

    public static void monitorThis(Object arg1, Object arg2) {
        if (arg1 != null && arg2 != null)
            Log.w(TAG, arg1.toString() + "\t" + arg2.toString());
    }

    public static void monitorThis(Object arg) {
        if (arg != null && MYLOG_WRITE_TO_FILE) {
            Log.w(TAG, arg.toString());
            filter(arg.toString());
        }
    }
    /**
     * 对信息进行上报
     */
    public static void reportError(String str, boolean local){
        str=getPackageInfo()+str;
        if(local){
            Log.e(TAG,str);
        }
        writeToFile(str.toString());

    }

    public static void reportError(){
        reportError("",true);
    }

    /**
     * 设置长达100s的时延，制造一个anr
     */
    public static void test_anr() {
        try {
            Thread.sleep(100000L);
        } catch (InterruptedException var3) {
            var3.printStackTrace();
        }

    }

    public static void test_native(){
        new monitor.NativeCrash.NativeTest().makeError();
    }

    /**
     * 对bugly给出的信息进行过滤筛选
     */
    private static void filter(String str){
        if(str.startsWith("#")){
            if(str.contains("Detail Record By Bugly")){
                logText = TAG+": \n";
            }
            else if(str.contains("CRASH TYPE")){
                logText+=(str+"\n");
            }
            else if(str.contains("APP VER")){
                logText+=(str+"\n");
            }
            else if(str.contains("CRASH DEVICE")){
                logText+=(str+"\n");
            }
            else if(str.contains("EXCEPTION STACK")){
                logText+=str;
            }
            else if(str.contains("#+++++++++")){
                logText+=(TAG+" End.");
                writeToFile(logText);
            }
            else if(str.startsWith("anr tm")){
                logText = TAG+": \n";
                logText+=(str+"\n"+TAG+" End.");
                writeToFile(logText);
            }
        }
    }

    /**
     * 获取系统和app相关信息
     */
    public static String getPackageInfo(){
        String info;
        try {
            Writer writer = new StringWriter();
            PrintWriter printWriter = new PrintWriter(writer);
            PackageInfo packageInfo = applicationContext.getPackageManager().getPackageInfo(applicationContext.getPackageName(), PackageManager.GET_ACTIVITIES);
            printWriter.println("App Version："+ packageInfo.versionName+"_"+packageInfo.versionCode);
            printWriter.println("Android Version："+ Build.VERSION.RELEASE+"_"+Build.VERSION.SDK_INT);
            printWriter.println("Vendor："+ Build.MANUFACTURER);
            printWriter.println("Model: " + Build.MODEL);
            info = writer.toString();
            printWriter.close();
        }
        catch(Exception ex){
            Log.e(TAG,"getPackageInfo failed.");
            ex.printStackTrace();
            return "";
        }
        return info;
    }

    private static String getFileName(){
        Date nowtime = new Date();
        String needWriteFiel = logfile.format(nowtime);
        MYLOG_PATH_SDCARD_DIR = Environment.getExternalStorageDirectory().getPath();
        String fileName = needWriteFiel + MYLOGFILEName;
        return fileName;
    }

    private static String getFilePath(){
        String fileName = getFileName();
        String filePath = MYLOG_PATH_SDCARD_DIR+"/"+fileName;
        return filePath;
    }
    /**
     * 写到文件，默认为在本地根目录
     */
    private static void writeToFile(String str){
        if(!MYLOG_WRITE_TO_FILE)
            return;
        String filePath = getFileName();
        Log.w(TAG,"Write to file "+MYLOG_PATH_SDCARD_DIR+"/"+filePath+"！");
        File file = new File(MYLOG_PATH_SDCARD_DIR,filePath);
        if (!file.exists()) {
            Log.w(TAG,"File doesn't exist，try to make file "+filePath+" in "+MYLOG_PATH_SDCARD_DIR+"！");
            file.getParentFile().mkdirs();
            try {
                file.createNewFile();
                Log.w(TAG,"Make file "+filePath+"！");
            } catch (IOException e) {
                Log.e(TAG,"Make file failed in "+MYLOG_PATH_SDCARD_DIR+"！");
                e.printStackTrace();
            }
        }
        try {
            Log.w(TAG,"Writing to "+filePath+"！");
            FileWriter filerWriter = new FileWriter(file, true);//后面这个参数代表是不是要接上文件中原来的数据，不进行覆盖
            BufferedWriter bufWriter = new BufferedWriter(filerWriter);
            bufWriter.write(str);
            bufWriter.newLine();
            bufWriter.close();
            filerWriter.close();
            Log.w(TAG,"Log success.");
        } catch (IOException e) {
            Log.e(TAG,"Log fail in "+filePath+"！");
            e.printStackTrace();
        }
    }
}