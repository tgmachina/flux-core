#!/usr/bin/env python

from __future__ import print_function

import os
import sys
import yaml
import inspect
import argparse
from datetime import datetime
from enum import Enum
from subprocess import Popen, PIPE


class ResourceManager(object):
    """Base class for describing a Resource Manager"""

    def __init__(self, jobspec):
        """Initializes with a flux jobspec

        """

        self.jobspec = yaml.safe_load(jobspec)

    def args(self):
        """Return the set of args to use, translating if available

        Translates a Flux jobspec into the set of args required by the
        underlying RM to get an allocation.

        Calls every defined map method with the loaded jobspec
        """

        args = (m() for name, m
                in inspect.getmembers(self, inspect.ismethod)
                if name.startswith("map_"))
        return [arg for arg in args if arg is not None]

    def submit(self, dry_run=False):
        raise NotImplementedError("must implement submit")


class Slurm(ResourceManager):

    default_shell = "/bin/sh"

    def job_script(self, args):
        """Takes a set of slurm args and outputs a jobscript
        """

        attributes = self.jobspec["attributes"]
        shell = self.default_shell

        if "user" in attributes:
            shell = attributes["user"].get("shell", shell)

        script = "#!{shell}\n".format(shell=shell)
        script += '\n'.join("#SBATCH " + arg for arg in args) + '\n'
        script += "srun flux start\n"

        return script

    def map_resource_cores(self):
        # TODO: right way to calculate this..
        # return "-c {:d}".format(num_cores)
        return

    def map_task_tasks(self):
        pass

    def map_resource_nodes(self):
        # Assuming (for now) [slots] -> nodes -> cores
        count = 0
        for resource in self.jobspec["resources"]:
            resource_type = resource["type"]
            if resource_type == "slot" or resource_type == "node":
                count += resource["count"]

            # check to see if any nodes are defined in "with"
            node_count = sum(r.get("count", 0)
                             for r in resource.get("with", [])
                             if r["type"] == "node")

            node_count = 1 if node_count == 0 else node_count
            count *= node_count

        return "-N {:d}".format(count)

    def map_attribute_system(self):
        args = []
        system = self.jobspec["attributes"].get("system", {})
        return "-t {}".format(system["duration"])

    def submit(self, dry_run=False):
        script = self.job_script(self.args())
        if dry_run:
            print(script)
            return
        cmd = ["sbatch"]

        # TODO: is subprocess the right way to go here? pexpect would be
        # easier to use, but add another dependency...
        # self.proc = Popen(cmd, stdin=PIPE, stdout=PIPE, stderr=PIPE)
        # out, err = self.proc.communicate(input=script)


class LSF(ResourceManager):
    pass


class Cobalt(ResourceManager):
    pass


ResourceManagers = Enum("ResourceManagers",
                        {rm.__name__: rm for rm in
                         ResourceManager.__subclasses__()})

def create_manager(manager_type, jobspec):
    """Create a new resource manager given a manager_type"""
    t = manager_type
    if t in ResourceManagers:
        return manager_type.value(jobspec)
    else:
        raise ValueError("manager type {t} not found".format(t=t))


def detect_manager(types):
    """Detect the resource manager on this host"""

    def which(cmd):
        all_paths = (os.path.join(path, cmd) for path in
                     os.environ["PATH"].split(os.pathsep))

        return any(
            os.access(path, os.X_OK) and os.path.isfile(path)
            for path in all_paths
        )

    if which("bsub"):
        return types.LSF
    elif which("salloc"):
        return types.Slurm
    elif which("cqsub"):
        return types.Cobalt
    else:
        raise OSError("unable to find a resource manager on this system")

def bootstrap(jobspec, dry_run=False):
    """Detect a resource manager and start flux with a given jobspec"""

    try:
        t = detect_manager(ResourceManagers)
        resource_manager = create_manager(t, jobspec)
        resource_manager.submit(dry_run=dry_run)
    except OSError as e:
        sys.exit(e)
    sys.exit(0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-d",
        "--dry-run",
        action="store_true",
        help="""Print the submission command or script"""
    )
    parser.add_argument("jobspec")
    args = parser.parse_args()
    bootstrap(args.jobspec, dry_run=args.dry_run)
