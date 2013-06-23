
package com.android.systemui.statusbar.toggles;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.view.View;

import com.android.systemui.R;

public class OrcaToolsToggle extends BaseToggle {

    @Override
    public void init(Context c, int style) {
        super.init(c, style);
        setIcon(R.drawable.ic_qs_bf_settings);
        setLabel(R.string.quick_settings_orcatools);
    }

    @Override
    public void onClick(View v) {
        try {
            Intent intent = new Intent("android.intent.action.MAIN");
            intent.setClassName("com.android.settings", "com.android.settings.Settings$OrcaToolsActivity");
            intent.addFlags(Intent.FLAG_FLOATING_WINDOW);
            startActivity(intent);
        } catch(NullPointerException e) {
            // No intent found for activity component
        }
        collapseStatusBar();
    }

    @Override
    public boolean onLongClick(View v) {
        Intent intent = new Intent("android.intent.action.MAIN");
        intent.setClassName("com.android.settings", "com.android.settings.Settings$OrcaToolsActivity");
        intent.addCategory("android.intent.category.LAUNCHER");
        collapseStatusBar();
        startActivity(intent);
        collapseStatusBar();
        return super.onLongClick(v);
    }
}
