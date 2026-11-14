def can_build(env, platform):
    # The engine library (SongoBGPlayer) is built for these. Windows is
    # in-process only: the daemon transport needs Unix sockets and fork.
    return platform in ("linuxbsd", "sbc", "android", "windows")


def configure(env):
    pass
