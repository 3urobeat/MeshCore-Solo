# Fixes to LVGL 9.2.2 (lib_deps) that ui-lvgl needs, applied in place to the
# downloaded library -- idempotent, so every build can run it.
#
#   PlatformIO:  extra_scripts = pre:examples/companion_radio/ui-lvgl/lvgl_patches.py
#                (patches as LVGL is compiled, i.e. after lib_deps are fetched)
#   The sim:     python3 examples/companion_radio/ui-lvgl/lvgl_patches.py <lvgl dir>
#
# Content height counted as scrolled (lv_obj_pos.c, calc_content_height): a
# SIZE_CONTENT box capped by max_height measures its children where the scroll
# has moved them, so an elastic pull past its end shrinks it by the overshoot,
# again on every pull (popups with a list). The function zeroes scroll.y for
# the count, but the children's coords already carry it; it's added back.
import os
import sys

PATCHES = [
    ("src/core/lv_obj_pos.c", [
        ("                    /*Normal top aligns. */\n"
         "                    child_res_tmp = child->coords.y2 - obj->coords.y1 + 1;\n",
         "                    /*Normal top aligns. */\n"
         "                    child_res_tmp = child->coords.y2 - obj->coords.y1 + 1 + scroll_y_tmp;\n"),
        ("        else {\n"
         "            child_res_tmp = child->coords.y2 - obj->coords.y1 + 1;\n"
         "        }\n",
         "        else {\n"
         "            child_res_tmp = child->coords.y2 - obj->coords.y1 + 1 + scroll_y_tmp;\n"
         "        }\n"),
    ]),
]


def apply(lvgl_dir):
    for rel, subs in PATCHES:
        path = os.path.join(lvgl_dir, rel)
        with open(path) as f:
            src = f.read()
        out = src
        for old, new in subs:
            if new in out:
                continue
            if out.count(old) != 1:
                raise RuntimeError("lvgl_patches: %s changed upstream, re-check the patch" % rel)
            out = out.replace(old, new)
        if out != src:
            with open(path, "w") as f:
                f.write(out)
            print("lvgl_patches: patched %s" % rel)


if __name__ == "__main__":
    apply(sys.argv[1])
else:
    Import("env")  # noqa: F821 (SCons)

    def _patch(env, node):
        path = node.srcnode().get_abspath()
        apply(path[: path.rindex(os.sep + "src" + os.sep)])
        return node

    env.AddBuildMiddleware(_patch, "*/lvgl/src/core/lv_obj_pos.c")  # noqa: F821
