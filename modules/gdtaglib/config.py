def can_build(env, platform):
    # build_music_records reads tags on std::thread workers.
    if platform == "web" and not env["threads"]:
        return False
    return True


def configure(env):
    pass
