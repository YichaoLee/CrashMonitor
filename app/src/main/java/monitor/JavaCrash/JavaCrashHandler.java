package monitor.JavaCrash;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.io.Writer;
import java.lang.Thread.UncaughtExceptionHandler;

import monitor.MonitorUtil;

/**
 * Created by leechyli on 2017/7/14.
 */

public class JavaCrashHandler implements UncaughtExceptionHandler{

    private static String TAG = MonitorUtil.TAG;
    private static JavaCrashHandler ourInstance = new JavaCrashHandler();
    private static Context ctx;

    public static JavaCrashHandler getInstance()
    {
        return ourInstance;
    }

    public static void registerJavaCrashHandler(Context c){
        ctx = c;
        Thread.setDefaultUncaughtExceptionHandler(JavaCrashHandler.getInstance());
        Log.w(TAG,"JavaCrashHandler has been registered.");
    }

    private JavaCrashHandler()
    {
    }

    @Override
    public void uncaughtException(Thread thread, final Throwable throwable) {
        /**
         * 捕获异常
         */
        Log.w(TAG, "in uncaughtException");
        String stackTraceInfo = getStackTraceInfo(throwable);
        MonitorUtil.reportError(stackTraceInfo, true);
        myUncaughtExceptionToDo();
    }

    /**
     * 自定义的对异常的处理
     */
    public void myUncaughtExceptionToDo() {
//        // 关闭应用
        System.exit(0);

    }


    /**
     * 获取Exception崩溃堆栈
     */
    public static String getStackTraceInfo(final Throwable ex) {
        String trace = "Error type: Java Crash\n";
        try {
            /**
             *获取调用此方法的application context
             */

            Writer writer = new StringWriter();
            PrintWriter printWriter = new PrintWriter(writer);
            ex.printStackTrace(printWriter);
            trace = writer.toString();
            printWriter.close();
        } catch (Exception e) {
            Log.e(TAG,"getStackTraceInfo failed.");
            ex.printStackTrace();
            return trace;
        }
        return trace;
    }
}

