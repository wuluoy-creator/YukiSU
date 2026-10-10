package com.zying.zysu;

import android.content.pm.PackageInfo;
import java.util.List;

interface IKsuInterface {
    int getPackageCount();
    List<PackageInfo> getPackages(int start, int maxCount);
    int refreshPackages();
    String[] getUidPackagesForBackup(int uid);
    String readProfileRulesForBackup(String packageName);
    boolean checkProfileRulesForBackup(String rules);
    boolean writeProfileRulesForBackup(String packageName, String rules);
}
