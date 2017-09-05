package monitor.NativeCrash;

/**
 * Created by leechyli on 2017/8/3.
 */


public class NativeTest{
    public void makeError(){
        nativeMakeError();
    }
    private native String nativeMakeError();
}
