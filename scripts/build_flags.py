import subprocess; 
import sys;
import os, re;

def git_branch():
    print('-D GIT_BRANCH=\\"%s\\"' % subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip().decode())

def git_repo():
    output = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'])
    repo = os.path.basename(output).strip().decode()
    print('-D GIT_REPO=\\"%s\\"' % repo);

def git_owner():
    # get the github owner of the origin repo
    output = subprocess.check_output(['git', 'config', '--get', 'remote.origin.url'])
    owner = re.search(r'github.com[:/](.*)/', output.decode()).group(1)
    print('-D GIT_OWNER=\\"%s\\"' % owner);


if __name__ == '__main__':
    globals()[sys.argv[1]]()
