#!/usr/bin/env python
# © Copyright 2016 CERN
#
# This software is distributed under the terms of the GNU General Public 
# Licence version 3 (GPL Version 3), copied verbatim in the file "LICENSE".
#
# In applying this licence, CERN does not waive the privileges and immunities
# granted to it by virtue of its status as an Intergovernmental Organization 
# or submit itself to any jurisdiction.
#
# Author: Grzegorz Jereczek <grzegorz.jereczek@cern.ch>
#

import httplib2
import json
import output

class RestError(Exception):
    """Represents a generic REST error."""
    pass

class RestClient(object):
    """Helper class to interact with the OpenDaylight's northbound REST interface."""
    def __init__(self, odl, user='admin', password='admin', silent = True):
        """Creates a new object of the RestClient class.
        
        Args:
            odl: Host name, where the OpenDaylight controller is running.
            user: OpenDaylight username.
            password: OpenDaylight password.
            silent: Supresses output, if False.
            
        Returns:
            An instance of the RestClient class.
        """
        self.__o = output.Output(silent)

        self.__host = odl
        self.__user = user
        self.__password = password
        self.__port = 8181

        self.__h = httplib2.Http(".cache", proxy_info=None)
        self.__h.add_credentials(self.__user, self.__password)

        self.__base_url = 'http://' + self.__host + ':' + str(self.__port) + '/restconf/'

    def __build_url(self, datastore, path):
        """Builds url to access path in a datastore.

        Args:
            datastore: The name of the datastore.
            path: Path to access in this datastore.

        Returns:
            Full url.
        """
        return self.__base_url + datastore + '/' + path

    def __check_response(self, resp, content):
        """Raises RestError, if request was not successfull.

        Args:
            resp: Response to the request.
            content: Content received.

        Returns:
            None.
        """
        if (resp['status'] != '200'):
            raise RestError(json.loads(content)['errors']['error'][0]['error-message'])
        else:
            self.__o.okgreen('Ok')

    def conf_ds_get(self, path):
        """Returns the content of the specified path in the configuration datastore."""
        self.__o.okblue('Requesting GET...')
        resp, content = self.__h.request(self.__build_url('config', path),
                                  "GET",
                                  headers = {'Content-Type' : 'application/json'})
        self.__check_response(resp, content)
        return json.loads(content)

    def conf_ds_put(self, path, content):
        """Puts the specified content to the specified path in the configuration datastore."""
        self.__o.okblue('Requesting PUT...')
        resp, content = self.__h.request(self.__build_url('config', path),
                                  "PUT",
                                  headers = {'Content-Type' : 'application/json'},
                                  body = json.dumps(content))
        self.__check_response(resp, content)

    def conf_ds_delete(self, path):
        """Deletes the path in the configuration datastore."""
        self.__o.okblue('Requesting DELETE...')
        resp, content = self.__h.request(self.__build_url('config', path),
                                  "DELETE",
                                  headers = {'Content-Type' : 'application/json'})
        self.__check_response(resp, content)

    def oper_ds_get(self, path):
        """Returns the content of the specified path in the operational datastore."""
        self.__o.okblue('Requesting GET...')
        resp, content = self.__h.request(self.__build_url('operational', path),
                                  "GET",
                                  headers = {'Content-Type' : 'application/json'})
        self.__check_response(resp, content)
        return json.loads(content)

