#!/usr/bin/python

import os
import sys, getopt
import json
import requests

import hashlib
import hmac
import base64
import serial
from decimal import *
from time import gmtime, strftime
from datetime import datetime
import socket
from uuid import getnode as get_mac

def main(argv):

    print 'Sync PI and server time'
    configFileName = 'config.json'
    json_data=open(configFileName)
    config_data = json.load(json_data)
    json_data.close()

    nowPI = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")

    href = config_data["Server"]["url"] + 'API/Device/GetServerDateTime'
    token = ComputeHash(nowPI, config_data["Server"]["key"])
    authentication = config_data["Server"]["id"] + ":" + token
    headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Authentication': authentication}
    print 'Server side URL:' + href
    r = requests.get(href, headers=headers, verify=False)
    if r.status_code == 200:
        nowPI = r.json()
        print ("Setting up time to: " + nowPI)
        os.popen('sudo -S date -s "' + nowPI + '"', 'w').write("123")
    else:
        print 'Error in setting time. Server response code: %i' % r.status_code


    href = config_data["Server"]["url"] + 'api/Device/DeviceConfigurationUpdate'
    token = ComputeHash(nowPI, config_data["Server"]["key"])
    authentication = config_data["Server"]["id"] + ":" + token
    print(authentication)
    headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': nowPI, 'Authentication': authentication}
    
    deviceDetail = {}
    deviceDetail["DeviceIdentifier"] = config_data["Server"]["Deviceid"]
    deviceDetail["DeviceType"] = "Custom"
    deviceDetail["DeviceConfigurations"] = [{'Key':'IPPrivate','Value':[(s.connect(('8.8.8.8', 80)), s.getsockname()[0], s.close()) for s in [socket.socket(socket.AF_INET, socket.SOCK_DGRAM)]][0][1]},
                                            {'Key':'IPPublic','Value': requests.get('http://icanhazip.com/').text},
                                            {'Key': 'Configuration', 'Value': json.dumps(config_data) },
                                            {'Key':'MAC','Value': ':'.join(("%012X" % get_mac())[i:i+2] for i in range(0, 12, 2))}
                                           ]

    payload = {'Device': deviceDetail}
    print 'Request Content: {0}'.format(json.dumps(payload))
    r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)

    if r.status_code == 200:
       print 'Response Content: {0}'.format(r.content)
       data = json.loads(r.text)    
       for entry in data['Device']['DeviceConfigurations']:
           if entry['Key'] == 'Configuration':     
              with open(configFileName, 'w') as outfile:
                 json.dump(json.loads(entry['Value']), outfile)
       print 'Device configuration Successfully updated'
    else:
       print 'Error in setting time. Server response code: {0} {1}'.format(r.status_code, r.content)




    href = config_data["Server"]["url"] + 'api/events/process'
    token = ComputeHash(nowPI, config_data["Server"]["key"])
    authentication = config_data["Server"]["id"] + ":" + token
    print(authentication)
    
    headers = {'Content-Type': 'application/json; charset=utf-8', 'Accept': 'application/json', 'Timestamp': nowPI, 'Authentication': authentication}
    measurements = []    

    measure = {}
    measure["EventType"] = 32
    measure["EventValue"] = 1
    measure["EventTime"] = nowPI
    measurements.append(measure)
       
    print measurements

    payload = {'events': measurements, "deviceId": config_data["Server"]["Deviceid"]}
    print(json.dumps(payload))
    r = requests.post(href, headers=headers, data=json.dumps(payload), verify=False)
    print (r)

    return '0'


def ComputeHash(timeStamp, key):
    message = bytes(timeStamp).encode('utf-8')
    secret  = bytes(key).encode('utf-8')
    signature = base64.b64encode(hmac.new(message, secret, digestmod=hashlib.sha256).digest())
    print (signature)
    return signature

if __name__ == '__main__':
    main(sys.argv[1:])