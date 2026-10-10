import itertools
import unittest

from plan_build import COMPONENTS, SUPPORTED_KMIS, resolve


class BuildPlanTests(unittest.TestCase):
    def test_default_is_complete_manager(self):
        selected, plan = resolve({})
        self.assertEqual([name for name in COMPONENTS if selected[name]], ["manager"])
        self.assertTrue(all(plan[name] for name in COMPONENTS))
        self.assertEqual(plan["kmi"], "all")

    def test_each_standalone_component(self):
        expected = {
            "manager": set(COMPONENTS),
            "ksud": {"ksud", "lkm", "ksuinit"},
            "lkm": {"lkm"},
            "ksuinit": {"ksuinit"},
        }
        for component, dependencies in expected.items():
            with self.subTest(component=component):
                _, plan = resolve({"build_manager": False, f"build_{component}": True})
                self.assertEqual({name for name in COMPONENTS if plan[name]}, dependencies)

    def test_combinations_preserve_selections_and_dependencies(self):
        for flags in itertools.product((False, True), repeat=len(COMPONENTS)):
            inputs = {f"build_{name}": flag for name, flag in zip(COMPONENTS, flags)}
            if not any(flags):
                with self.assertRaises(ValueError):
                    resolve(inputs)
                continue
            selected, plan = resolve(inputs)
            self.assertTrue(all(plan[name] for name in COMPONENTS if selected[name]))
            if plan["manager"]:
                self.assertTrue(plan["ksud"])
            if plan["ksud"]:
                self.assertTrue(plan["lkm"] and plan["ksuinit"])

    def test_kmi_selection(self):
        for kmi in SUPPORTED_KMIS:
            self.assertEqual(resolve({"kmi": kmi})[1]["kmi"], kmi)
        self.assertEqual(
            resolve({"kmi": " android14-6.1, android15-6.6,android14-6.1 "})[1]["kmi"],
            "android14-6.1,android15-6.6",
        )
        for invalid in (
            "", "all,android14-6.1", "android99-9.9", "android14-6.1,", "$(id)",
            "android12-5.10", "android13-5.10", "android13-5.15", "android14-5.15",
            "android14-6.1,android14-5.15",
        ):
            with self.subTest(kmi=invalid), self.assertRaises(ValueError):
                resolve({"kmi": invalid})

    def test_rejects_string_booleans(self):
        for key in (f"build_{name}" for name in COMPONENTS):
            with self.assertRaises(ValueError):
                resolve({key: "false"})


if __name__ == "__main__":
    unittest.main()
