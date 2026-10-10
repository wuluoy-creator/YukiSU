#!/usr/bin/env python3
"""Resolve user selections into a build graph before starting expensive jobs."""

import json
import os
from pathlib import Path


SUPPORTED_KMIS = (
    "android14-6.1",
    "android15-6.6",
    "android16-6.12",
    "android17-6.18",
)
COMPONENTS = ("manager", "lkm", "ksud", "ksuinit")


def resolve(inputs):
    selected = {}
    for component in COMPONENTS:
        value = inputs.get(f"build_{component}", component == "manager")
        if not isinstance(value, bool):
            raise ValueError(f"build_{component} must be a boolean")
        selected[component] = value
    if not any(selected.values()):
        raise ValueError("请至少勾选一个编译组件 / Select at least one component")

    kmi_input = inputs.get("kmi", "all").strip()
    kmis = list(SUPPORTED_KMIS) if kmi_input == "all" else list(
        dict.fromkeys(item.strip() for item in kmi_input.split(","))
    )
    if any(kmi not in SUPPORTED_KMIS for kmi in kmis):
        raise ValueError(f"Unsupported KMI selection: {kmi_input}")

    plan = selected.copy()
    plan["ksud"] = selected["ksud"] or selected["manager"]
    plan["lkm"] = selected["lkm"] or plan["ksud"]
    plan["ksuinit"] = selected["ksuinit"] or plan["ksud"]
    plan["kmi"] = "all" if kmi_input == "all" else ",".join(kmis)
    return selected, plan


def main():
    selected, plan = resolve(json.loads(os.environ.get("BUILD_INPUTS", "{}")))
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as output:
        for key, value in plan.items():
            rendered = str(value).lower() if isinstance(value, bool) else value
            output.write(f"{key}={rendered}\n")
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as summary:
        summary.write("## 编译计划 / Build plan\n\n")
        summary.write("| 组件 / Component | 状态 / Status |\n| --- | --- |\n")
        for component in COMPONENTS:
            status = "已选择 / Selected" if selected[component] else (
                "自动依赖 / Dependency" if plan[component] else "跳过 / Skipped"
            )
            summary.write(f"| {component} | {status} |\n")
        if plan["lkm"]:
            summary.write(f"\nKMI: `{plan['kmi']}`\n")
        if plan["manager"]:
            summary.write("\n完成后下载 **Manager-arm64-v8a** 中的 APK。"
                          "`Manager-gradle-arm64-v8a` 是尚未嵌入 ksud 的中间产物。\n")
        if plan["lkm"] and plan["kmi"] != "all":
            summary.write("\n本次仅包含所选 KMI；Manager/ksud 不含其他内核版本的模块。\n")


if __name__ == "__main__":
    main()
