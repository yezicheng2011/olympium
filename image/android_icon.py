"""Bounded APK icons; supported Android vector/adaptive drawables become SVG.

Unknown types return no preview. Generated SVG has no external resource URLs.
"""
import json
import re
import struct
import xml.etree.ElementTree as ET
import zipfile


def apk_icon(apk, badging, aapt, output):
    resources = None
    def resolve(value, depth=0):
        nonlocal resources
        if depth > 8:
            raise ValueError('Recursive drawable')
        if not value.startswith('@0x'):
            return value
        if resources is None:
            resources = aapt('dump', '--values', 'resources', str(apk))
        match = re.search(r'^        resource ' + re.escape(value[1:]) +
                          r' .*?\n          (.+)$', resources, re.M)
        if not match:
            raise ValueError('Unresolved drawable')
        data = match[1]
        if data.startswith('(reference) '):
            return resolve('@' + data.split()[-1], depth + 1)
        if data.startswith('(color) '):
            return data.split()[-1]
        if data.startswith(('(string8) ', '(string16) ')):
            return json.loads(data[data.index('"'):])
        raise ValueError('Unsupported resource')

    def tree(member):
        if not member.startswith('res/') or '..' in member.split('/'):
            raise ValueError('Invalid resource path')
        text = aapt('dump', 'xmltree', str(apk), member)
        nodes = []
        root = None
        for line in text.splitlines():
            match = re.match(r'( *)E: ([A-Za-z-]+) ', line)
            if match:
                indent = len(match[1])
                node = ET.Element(match[2])
                while nodes and nodes[-1][0] >= indent:
                    nodes.pop()
                if nodes:
                    nodes[-1][1].append(node)
                else:
                    root = node
                nodes.append((indent, node))
            else:
                match = re.match(r' *A: android:([A-Za-z]+)\([^)]*\)=(.*)', line)
                if match and nodes:
                    value = match[2]
                    if value.startswith('"'):
                        value = json.JSONDecoder().raw_decode(value)[0]
                    nodes[-1][1].set(match[1], value)
        if root is None:
            raise ValueError('No drawable root')
        return root

    def number(value, default):
        if value is None:
            return default
        match = re.fullmatch(r'\(type 0x4\)0x([0-9a-fA-F]+)', value)
        result = struct.unpack('!f', bytes.fromhex(match[1].zfill(8)))[0] if match else float(value)
        if not -10000 < result < 10000:
            raise ValueError('Drawable coordinate out of bounds')
        return result

    def color(value):
        value = resolve(value)
        match = re.fullmatch(r'\(type 0x1[c-f]\)0x([0-9a-fA-F]{8})', value)
        if match:
            value = '#' + match[1]
        if not re.fullmatch(r'#[0-9a-fA-F]{8}', value):
            raise ValueError('Unsupported color')
        return '#' + value[3:], int(value[1:3], 16) / 255

    def render(node, parent):
        if node.tag == 'path':
            path = node.get('pathData', '')
            if len(path) > 100000 or not re.fullmatch(r'[MmZzLlHhVvCcSsQqTtAa0-9eE+.,\s-]+', path):
                raise ValueError('Unsupported vector path')
            fill, opacity = color(node.get('fillColor', '#ff000000'))
            attrs = {'d': path, 'fill': fill, 'fill-opacity': str(opacity)}
            if 'strokeColor' in node.attrib:
                stroke, alpha = color(node.get('strokeColor'))
                attrs.update(stroke=stroke, **{'stroke-opacity': str(alpha),
                             'stroke-width': str(number(node.get('strokeWidth'), 0))})
            ET.SubElement(parent, 'path', attrs)
        elif node.tag == 'group':
            tx, ty = number(node.get('translateX'), 0), number(node.get('translateY'), 0)
            px, py = number(node.get('pivotX'), 0), number(node.get('pivotY'), 0)
            sx, sy = number(node.get('scaleX'), 1), number(node.get('scaleY'), 1)
            rotation = number(node.get('rotation'), 0)
            group = ET.SubElement(parent, 'g', transform=(
                f'translate({tx} {ty}) translate({px} {py}) rotate({rotation}) '
                f'scale({sx} {sy}) translate({-px} {-py})'))
            for child in node:
                render(child, group)
        else:
            raise ValueError('Unsupported vector element')

    try:
        icons = re.findall(r"^application-icon-(\d+):'([^']+)'$", badging, re.M)
        if not icons:
            return None
        member = max(icons, key=lambda pair: int(pair[0]))[1]
        with zipfile.ZipFile(apk) as archive:
            entry = archive.getinfo(member)
            if entry.file_size > 4 * 1024 * 1024:
                raise ValueError('Icon too large')
            if member.endswith('.png'):
                data = archive.read(entry)
                if data[:8] != b'\x89PNG\r\n\x1a\n' or len(data) < 24:
                    raise ValueError('Invalid PNG')
                width, height = struct.unpack('!II', data[16:24])
                if not 0 < width <= 2048 or not 0 < height <= 2048:
                    raise ValueError('Icon dimensions too large')
                output = output.with_suffix('.png')
                output.write_bytes(data)
                return output
        node = tree(member)
        svg = ET.Element('svg', xmlns='http://www.w3.org/2000/svg', width='128', height='128')
        if node.tag == 'adaptive-icon':
            background, foreground = node.find('background'), node.find('foreground')
            if background is None or foreground is None:
                raise ValueError('Incomplete adaptive icon')
            fill, opacity = color(background.get('drawable', ''))
            svg.set('viewBox', '0 0 108 108')
            ET.SubElement(svg, 'rect', width='108', height='108', rx='24',
                          fill=fill, **{'fill-opacity': str(opacity)})
            node = tree(resolve(foreground.get('drawable', '')))
            layer = ET.SubElement(svg, 'svg', width='108', height='108')
        else:
            layer = svg
        if node.tag != 'vector':
            raise ValueError('Unsupported icon type')
        width, height = number(node.get('viewportWidth'), 24), number(node.get('viewportHeight'), 24)
        if width <= 0 or height <= 0:
            raise ValueError('Empty vector viewport')
        layer.set('viewBox', f'0 0 {width} {height}')
        for child in node:
            render(child, layer)
        output = output.with_suffix('.svg')
        ET.ElementTree(svg).write(output, encoding='utf-8', xml_declaration=True)
        return output
    except (ValueError, KeyError, zipfile.BadZipFile):
        return None
