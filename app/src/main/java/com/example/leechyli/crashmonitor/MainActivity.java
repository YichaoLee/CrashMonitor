package com.example.leechyli.crashmonitor;

import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;
import android.widget.Button;

import monitor.MonitorUtil;
import monitor.SystemManager;

public class MainActivity extends AppCompatActivity implements View.OnClickListener {

    // Used to load the 'native-lib' library on application startup.

    private Button btnTestJavaCrash;
    private Button btnTestANRCrash;
    private Button btnTestNativeCrash;



    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        Log.w(MonitorUtil.TAG, "Activity create.\n");

//        String apkRoot="chmod 777 "+getPackageCodePath();
//        SystemManager.RootCommand(apkRoot);


        btnTestJavaCrash = (Button) findViewById(R.id.btnTestJavaCrash);
        btnTestANRCrash = (Button) findViewById(R.id.btnTestANRCrash);
        btnTestNativeCrash = (Button)findViewById(R.id.btnTestNativeCrash);
        btnTestJavaCrash.setOnClickListener(this);
        btnTestANRCrash.setOnClickListener(this);
        btnTestNativeCrash.setOnClickListener(this);
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.btnTestJavaCrash: // 点击测试Java Crash
                String str = null;
                Log.w(str,str);
                break;
            case R.id.btnTestANRCrash: // 点击测试ANR Crash
                MonitorUtil.test_anr();
                break;
            case R.id.btnTestNativeCrash: // 点击测试Native Crash
                MonitorUtil.test_native();
                break;
        }
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
}
