import json
import pytest
import conftest
import os

@pytest.fixture()
def json_doc():
    json_filepath = os.path.join(conftest.BINDIR, 'doc/hlwm-doc.json')
    with open(json_filepath, 'r') as fh:
        doc = json.loads(fh.read())
    return doc

def create_client(hlwm):
    winid, _ = hlwm.create_client()
    return f'clients.{winid}'

def create_frame_split(hlwm):
    hlwm.call('split explode')
    return 'tags.0.tiling.root'

@pytest.mark.parametrize('clsname,object_path',[
    ('ByName', 'monitors.by-name'),
    ('Client', create_client),
    ('ClientManager', 'clients'),
    ('DecTriple', 'theme.tiling'),
    ('DecorationScheme', 'theme.tiling.urgent'),
    ('FrameLeaf', 'tags.0.tiling.root'),
    ('FrameSplit', create_frame_split),
    ('HSTag', 'tags.0'),
    ('Monitor', 'monitors.0'),
    ('MonitorManager', 'monitors'),
    ('Root', ''),
    ('Settings', 'settings'),
    ('TagManager', 'tags'),
    ('Theme', 'theme'),
])
def test_attributes(hlwm, clsname, object_path, json_doc):
    object_doc = None
    for obj in json_doc['objects']:
        if obj['classname'] == clsname:
            object_doc = obj
            break
    assert object_doc is not None

    if not isinstance(object_path, str):
        # if it's not a string directly, it's a function returning
        # a string
        object_path = object_path(hlwm)

    # 1. test that all documented attributes actually exist
    for attr in object_doc['attributes']:
        print("checking attribute {}::{}".format(clsname, attr['cpp_name']))
        hlwm.get_attr('{}.{}'.format(object_path,attr['name']).lstrip('.'))



