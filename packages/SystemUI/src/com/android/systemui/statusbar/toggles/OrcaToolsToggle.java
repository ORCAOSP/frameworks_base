
package com.android.systemui.statusbar.toggles;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.view.View;

import com.android.systemui.R;

public class OrcaToolsToggle extends BaseToggle {

    @Override
    protected void init(Context c, int style) {
        super.init(c, style);
        setIcon(R.drawable.ic_qs_orca);
        setLabel(R.string.quick_settings_orcatools);
    }

    @Override
    public void onClick(View v) {
        Intent intent = new Intent("android.intent.action.MAIN");
        intent.setClassName("com.android.settings", "com.android.settings.Settings$OrcaToolsActivity");
        intent.addCategory("android.intent.category.LAUNCHER");

        collapseStatusBar();
        dismissKeyguard();
        startActivity(intent);
    }

}
