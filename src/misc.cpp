/******************************************************************************
 * Copyright (C) 2019-2025 debugger999 <debugger999@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include "misc.h"
#include "master.h"
#include "system.h"
#include "shm.h"
#include "obj.h"
#include "task.h"
#include "rest.h"
#include "alg.h"

static int createDir(const char *sPathName) {
    char dirName[4096];
    strcpy(dirName, sPathName);

    int len = strlen(dirName);
    if(dirName[len-1]!='/')
        strcat(dirName, "/");

    len = strlen(dirName);
    int i=0;
    for(i=1; i<len; i++)
    {
        if(dirName[i]=='/')
        {
            dirName[i] = 0;
            if(access(dirName, R_OK)!=0)
            {
                if(mkdir(dirName, 0755)==-1)
                {
                    //TODO:errno:ENOSPC(No space left on device)
                    app_err("path %s error %s", dirName, strerror(errno));
                    return -1;
                }
            }
            dirName[i] = '/';
        }
    }
    return 0;
}

int dirCheck(const char *dir) {
    DIR *pdir = opendir(dir);
    if(pdir == NULL)
        return createDir(dir);
    else
        return closedir(pdir);   
}

int conditionByObjId(node_common *p, void *arg) {
    int id = *(int *)arg;
    objParam *pObjParam = (objParam *)p->name;
    return id == pObjParam->id;
}

int conditionBySlaveIp(node_common *p, void *arg) {
    char *slaveIp = (char *)arg;
    slaveParams *pSlaveParams = (slaveParams *)p->name;
    return !strcmp(slaveIp, pSlaveParams->ip);
}


int systemInit(char *buf, aiotcParams *pAiotcParams) {
    shmParams *pShmParams = (shmParams *)pAiotcParams->shmArgs;
    systemParams *pSystemParams = (systemParams *)pAiotcParams->systemArgs;

    if(pSystemParams->sysOrgData != NULL) {
        shmFree(pShmParams->headsp, pSystemParams->sysOrgData);
    }
    pSystemParams->sysOrgData = (char *)shmMalloc(pShmParams->headsp, strlen(buf) + 1);
    if(pSystemParams->sysOrgData == NULL) {
        app_err("shm malloc failed, %ld", strlen(buf) + 1);
        return -1;
    }
    strcpy(pSystemParams->sysOrgData, buf);

    return 0;
}

static int initObjTask(char *buf, objParam *pObjParam) {
    int livestream, capture, record;
    char *slaveIp = NULL, *preview = NULL;
    taskParams *pTaskParams = (taskParams *)pObjParam->task;
    aiotcParams *pAiotcParams = (aiotcParams *)pObjParam->arg;
    masterParams *pMasterParams = (masterParams *)pAiotcParams->masterArgs;

    slaveIp = getStrValFromJson(buf, "slave", "ip", NULL);
    livestream = getIntValFromJson(buf, "task", "stream", NULL);
    capture = getIntValFromJson(buf, "task", "capture", NULL);
    record = getIntValFromJson(buf, "task", "record", NULL);
    preview = getStrValFromJson(buf, "task", "preview", NULL);
    if(livestream >= 0) {
        pTaskParams->livestream = livestream;
    }
    if(capture >= 0) {
        pTaskParams->capture = capture;
    }
    if(record >= 0) {
        pTaskParams->record = record;
    }
    if(preview != NULL && strlen(preview) > 0) {
        strncpy(pTaskParams->preview, preview, sizeof(pTaskParams->preview));
        free(preview);
    }
    if(slaveIp != NULL) {
        node_common *p = NULL;
        semWait(&pMasterParams->mutex_slave);
        searchFromQueue(&pMasterParams->slaveQueue, slaveIp, &p, conditionBySlaveIp);
        semPost(&pMasterParams->mutex_slave);
        if(p != NULL) {
            pObjParam->slave = p->name;
        }
        free(slaveIp);
    }

    return 0;
}

int addObj(char *buf, aiotcParams *pAiotcParams, int max, void *arg) {
    node_common node;
    taskParams *pTaskParams;
    char *name = NULL, *type = NULL, *subtype = NULL;
    objParam *pObjParam = (objParam *)node.name;
    CommonParams *pParams = (CommonParams *)arg;
    sem_t *mutex = (sem_t *)pParams->arga;
    queue_common *queue = (queue_common *)pParams->argb;
    shmParams *pShmParams = (shmParams *)pAiotcParams->shmArgs;

    memset(&node, 0, sizeof(node));
    name = getStrValFromJson(buf, "name", NULL, NULL);
    type = getStrValFromJson(buf, "type", NULL, NULL);
    subtype = getStrValFromJson(buf, "data", "subtype", NULL);
    pObjParam->id = getIntValFromJson(buf, "id", NULL, NULL);
    if(pObjParam->id < 0 || name == NULL || type == NULL || subtype == NULL) {
        goto end;
    }
    strncpy(pObjParam->name, name, sizeof(pObjParam->name));
    strncpy(pObjParam->type, type, sizeof(pObjParam->type));
    strncpy(pObjParam->subtype, subtype, sizeof(pObjParam->subtype));

    pObjParam->task = shmMalloc(pShmParams->headsp, sizeof(taskParams));
    pObjParam->originaldata = (char *)shmMalloc(pShmParams->headsp, strlen(buf) + 1);
    if(pObjParam->task == NULL || pObjParam->originaldata == NULL) {
        app_err("shm malloc failed");
        goto end;
    }
    memset(pObjParam->task, 0, sizeof(taskParams));
    strcpy((char *)pObjParam->originaldata, buf);
    pTaskParams = (taskParams *)pObjParam->task;
    if(sem_init(&pTaskParams->mutex_alg, 1, 1) < 0) {
      app_err("sem init failed");
      goto end;
    }
    pObjParam->arg = pAiotcParams;
    initObjTask(buf, pObjParam);

    semWait(mutex);
    putToShmQueue(pShmParams->headsp, queue, &node, max);
    semPost(mutex);

end:
    if(name != NULL) {
        free(name);
    }
    if(type != NULL) {
        free(type);
    }
    if(subtype != NULL) {
        free(subtype);
    }
    return 0;
}

int delObj(char *buf, aiotcParams *pAiotcParams, void *arg) {
    int id;
    node_common *p = NULL;
    CommonParams *pParams = (CommonParams *)arg;
    sem_t *mutex = (sem_t *)pParams->arga;
    queue_common *queue = (queue_common *)pParams->argb;
    shmParams *pShmParams = (shmParams *)pAiotcParams->shmArgs;

    //TODO : stop something first
    id = getIntValFromJson(buf, "id", NULL, NULL);
    semWait(mutex);
    delFromQueue(queue, &id, &p, conditionByObjId);
    semPost(mutex);
    if(p != NULL) {
        objParam *pObjParam = (objParam *)p->name;
        if(pObjParam->task != NULL) {
            taskParams *pTaskParams = (taskParams *)pObjParam->task;
            semWait(&pTaskParams->mutex_alg);
            freeShmQueue(pShmParams->headsp, &pTaskParams->algQueue, NULL);
            semPost(&pTaskParams->mutex_alg);
            sem_destroy(&pTaskParams->mutex_alg);
            shmFree(pShmParams->headsp, pObjParam->task);
        }
        if(pObjParam->originaldata != NULL) {
            shmFree(pShmParams->headsp, pObjParam->originaldata);
        }
        if(p->arg != NULL) {
            shmFree(pShmParams->headsp, p->arg);
        }
        shmFree(pShmParams->headsp, p);
    }

    return 0;
}

int httpPostSlave(const char *url, char *buf, objParam *pObjParam) {
    char urladdr[256];
    slaveParams *pSlaveParams = (slaveParams *)pObjParam->slave;

    if(pObjParam->slave != NULL && pObjParam->attachSlave) {
        snprintf(urladdr, sizeof(urladdr), "http://%s:%d%s", pSlaveParams->ip, pSlaveParams->restPort, url);
        httpPost(urladdr, buf, NULL, 3);
    }

    return 0;
}

int addAlg(char *buf, int id, char *algName, aiotcParams *pAiotcParams, void *arg) {
    node_common node;
    node_common *p = NULL;
    CommonParams *pParams = (CommonParams *)arg;
    sem_t *mutex = (sem_t *)pParams->arga;
    queue_common *queue = (queue_common *)pParams->argb;
    shmParams *pShmParams = (shmParams *)pAiotcParams->shmArgs;

    memset(&node, 0, sizeof(node));
    semWait(mutex);
    searchFromQueue(queue, &id, &p, conditionByObjId);
    if(p != NULL) {
        objParam *pObjParam = (objParam *)p->name;
        taskParams *pTaskParams = (taskParams *)pObjParam->task;
        algParams *pAlgParams = (algParams *)node.name;
        strncpy(pAlgParams->name, algName, sizeof(pAlgParams->name));
        semWait(&pTaskParams->mutex_alg);
        putToShmQueue(pShmParams->headsp, &pTaskParams->algQueue, &node, 100);
        semPost(&pTaskParams->mutex_alg);
        if(pParams->argc != NULL) {
            httpPostSlave("/task/start", buf, pObjParam);
        }
    }
    else {
        printf("objId %d not exsit\n", id);
    }
    semPost(mutex);
    
    return 0;
}

int conditionByAlgName(node_common *p, void *arg) {
    char *algName = (char *)arg;
    algParams *pAlgParams = (algParams *)p->name;
    return !strncmp(algName, pAlgParams->name, sizeof(pAlgParams->name));
}

int delAlg(char *buf, int id, char *algName, aiotcParams *pAiotcParams, void *arg) {
    node_common *p = NULL, *palg = NULL;
    CommonParams *pParams = (CommonParams *)arg;
    sem_t *mutex = (sem_t *)pParams->arga;
    queue_common *queue = (queue_common *)pParams->argb;
    shmParams *pShmParams = (shmParams *)pAiotcParams->shmArgs;

    semWait(mutex);
    searchFromQueue(queue, &id, &p, conditionByObjId);
    if(p != NULL) {
        objParam *pObjParam = (objParam *)p->name;
        taskParams *pTaskParams = (taskParams *)pObjParam->task;
        semWait(&pTaskParams->mutex_alg);
        delFromQueue(&pTaskParams->algQueue, algName, &palg, conditionByAlgName);
        semPost(&pTaskParams->mutex_alg);
        if(palg != NULL) {
            if(palg->arg != NULL) {
                shmFree(pShmParams->headsp, palg->arg);
            }
            shmFree(pShmParams->headsp, palg);
        }
        if(pParams->argc != NULL) {
            httpPostSlave("/task/stop", buf, pObjParam);
        }
    }
    else {
        printf("objId %d not exsit\n", id);
    }
    semPost(mutex);
    
    return 0;
}

