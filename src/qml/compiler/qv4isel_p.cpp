/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** Copyright (C) 2015 Nomovok Ltd. All rights reserved.
** Contact: info@nomovok.com
**
** Copyright (C) 2015 Canonical Limited and/or its subsidiary(-ies).
** Contact: ricardo.mendoza@canonical.com
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtCore/QDebug>
#include <QtCore/QBuffer>
#include "qv4jsir_p.h"
#include "qv4isel_p.h"
#include "qv4isel_util_p.h"
#include <private/qv4value_p.h>
#ifndef V4_BOOTSTRAP
#include <private/qqmlpropertycache_p.h>
#endif

#include <QString>
#include <QDataStream>

#ifndef V4_UNIT_CACHE
#undef ENABLE_UNIT_CACHE
#endif

#ifdef ENABLE_UNIT_CACHE
#include <private/qqmltypenamecache_p.h>
#include <private/qqmlcompiler_p.h>
#include <private/qqmltypeloader_p.h>
#include <private/qv4compileddata_p.h>
#include <private/qv4assembler_p.h>
#include "../jit/qv4cachedlinkdata_p.h"
#include "../jit/qv4assembler_p.h"
#include <sys/stat.h>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QBuffer>
#endif

bool writeData(QDataStream& stream, const char* data, int len)
{
    if (stream.writeRawData(data, len) != len)
        return false;
    else
        return true;
}

bool writeDataWithLen(QDataStream& stream, const char* data, int len)
{
    quint32 l = len;
    if (!writeData(stream, (const char *)&l, sizeof(quint32)))
        return false;
    if (!writeData(stream, data, len))
        return false;
    return true;
}

bool readData(char *data, int len, QDataStream &stream)
{
    if (stream.readRawData(data, len) != len) {
        return false;
    } else {
        return true;
    }
}

using namespace QV4;
using namespace QV4::IR;

static bool do_cache = false;

enum CacheState {
    UNTESTED = 0,
    VALID = 1,
    INVALID = 2
};

EvalInstructionSelection::EvalInstructionSelection(QV4::ExecutableAllocator *execAllocator, Module *module, QV4::Compiler::JSUnitGenerator *jsGenerator)
    : useFastLookups(true)
    , useTypeInference(true)
    , executableAllocator(execAllocator)
    , irModule(module)
{
    if (!jsGenerator) {
        jsGenerator = new QV4::Compiler::JSUnitGenerator(module);
        ownJSGenerator.reset(jsGenerator);
    }
    this->jsGenerator = jsGenerator;
#ifndef V4_BOOTSTRAP
    Q_ASSERT(execAllocator);
#endif
    Q_ASSERT(module);

    // Enable JIT cache only when explicitly requested and only cache files-on-disk (no qrc or inlines)
    do_cache = !qgetenv("QV4_ENABLE_JIT_CACHE").isEmpty() && irModule->fileName.startsWith(QStringLiteral("file://"));
}

EvalInstructionSelection::~EvalInstructionSelection()
{}

EvalISelFactory::~EvalISelFactory()
{}

QQmlRefPointer<QV4::CompiledData::CompilationUnit> EvalInstructionSelection::runAll(bool generateUnitData)
{
    for (int i = 0; i < irModule->functions.size(); ++i) {
        run(i); // Performs the actual compilation
    }

    QQmlRefPointer<QV4::CompiledData::CompilationUnit> result = backendCompileStep();

#ifdef ENABLE_UNIT_CACHE
    result->isRestored = false;
#endif

    if (generateUnitData)
        result->data = jsGenerator->generateUnit();

    return result;
}

QQmlRefPointer<QV4::CompiledData::CompilationUnit> EvalInstructionSelection::compile(bool generateUnitData)
{
#ifndef ENABLE_UNIT_CACHE
    return runAll(generateUnitData);
#else
    QQmlRefPointer<QV4::CompiledData::CompilationUnit> result(nullptr);

    // Check if running JIT mode and if cache is enabled
    if (!do_cache || !this->impl()) {
        return runAll(generateUnitData);
    }

    QV4::CompiledData::CompilationUnit *unit;
    bool loaded = false;
    bool do_save = true;

    QByteArray path(qgetenv("HOME") + QByteArray("/.cache/QML/Apps/") + (qgetenv("APP_ID").isEmpty() ? QCoreApplication::applicationName().toLatin1() : qgetenv("APP_ID")));

    if (m_engine && m_engine->qmlCacheValid == CacheState::UNTESTED) {
        bool valid = true;
        QDir cacheDir;
        cacheDir.setPath(QLatin1String(path));
        QStringList files = cacheDir.entryList();
        for (int i = 0; i < files.size(); i++) {
            if (valid == false)
                break;

            QFile cacheFile(path + QDir::separator() + files.at(i));
            if (cacheFile.open(QIODevice::ReadOnly)) {
                QDataStream stream(&cacheFile);
                quint32 strLen = 0;
                readData((char *)&strLen, sizeof(quint32), stream);
                if (strLen == 0) {
                    cacheFile.close();
                    continue;
                }

                char *tmpStr = (char *) malloc(strLen);
                readData((char *)tmpStr, strLen, stream);
                quint32 mtime = 0;
                readData((char *)&mtime, sizeof(quint32), stream);

                struct stat sb;
                stat(tmpStr, &sb);
                if (QFile::exists(QLatin1String(tmpStr))) {
                    QByteArray time(ctime(&sb.st_mtime));
                    if (mtime != (quint32) sb.st_mtime) {
                        if (m_engine) m_engine->qmlCacheValid = CacheState::INVALID;
                        valid = false;
                    }
                } else {
                    // Compilation unit of unresolvable type (inline), remove
                    cacheFile.remove();
                }

                free(tmpStr);
                cacheFile.close();
            }
        }
        if (valid) {
            m_engine->qmlCacheValid = CacheState::VALID;
        } else {
            for (int i = 0; i < files.size(); i++)
                cacheDir.remove(files.at(i));
        }
    }

    // Search for cache blob by mtime/app_id/file hash
    struct stat sb;
    stat(irModule->fileName.toLatin1().remove(0, 7).constData(), &sb);
    QByteArray time(ctime(&sb.st_mtime));
    QByteArray urlHash(QCryptographicHash::hash((irModule->fileName.toLatin1() + qgetenv("APP_ID") + time), QCryptographicHash::Md5).toHex());
    QDir dir;
    dir.mkpath(QLatin1String(path));
    QFile cacheFile(path + QDir::separator() + urlHash);

    QByteArray fileData;

    // TODO: Support inline compilation units
    if (cacheFile.exists() && cacheFile.open(QIODevice::ReadOnly)) {
        if (!irModule->fileName.isEmpty() && !irModule->fileName.contains(QStringLiteral("inline")) && m_engine) {
            loaded = true;
            fileData.append(cacheFile.readAll());
        } else {
            loaded = false;
            cacheFile.close();
            return runAll(generateUnitData);
        }
    }

    // Check file integrity
    QString fileHash(QLatin1String(QCryptographicHash::hash(fileData.left(fileData.size()-32), QCryptographicHash::Md5).toHex()));
    if (!fileHash.contains(QLatin1String(fileData.right(32)))) {
        cacheFile.close();
        cacheFile.remove();
        loaded = false;
    }

    // This code has been inspired and influenced by Nomovok's QMLC compiler available at
    // https://github.com/qmlc/qmlc. All original Copyrights are maintained for the
    // basic code snippets.
    if (loaded) {
        // Retrieve unit skeleton from isel implementation
        unit = mutableCompilationUnit();
        QV4::JIT::CompilationUnit *tmpUnit = (QV4::JIT::CompilationUnit *) unit;
        tmpUnit->codeRefs.resize(irModule->functions.size());

        QDataStream stream(fileData);

        quint32 strLen = 0;
        readData((char *)&strLen, sizeof(quint32), stream);
        char *tmpStr = (char *) malloc(strLen);
        readData((char *)tmpStr, strLen, stream);
        quint32 mtime = 0;
        readData((char *)&mtime, sizeof(quint32), stream);

        unit->lookupTable.reserve(tmpUnit->codeRefs.size());
        quint32 len;
        for (int i = 0; i < tmpUnit->codeRefs.size(); i++) {
            quint32 strLen;
            readData((char *)&strLen, sizeof(quint32), stream);

            char *fStr = (char *) malloc(strLen);
            readData(fStr, strLen, stream);

            QString hashString(QLatin1String(irModule->functions.at(i)->name->toLatin1().constData()));
            hashString.append(QString::number(irModule->functions.at(i)->line));
            hashString.append(QString::number(irModule->functions.at(i)->column));

            if (!hashString.contains(QLatin1String(fStr)))
                return runAll(generateUnitData);

            unit->lookupTable.append(i);

            len = 0;
            readData((char *)&len, sizeof(quint32), stream);

            // Temporary unlinked code buffer
            executableAllocator->allocate(len);
            char *data = (char *) malloc(len);
            readData(data, len, stream);

            quint32 linkCallCount = 0;
            readData((char *)&linkCallCount, sizeof(quint32), stream);

            QVector<QV4::JIT::CachedLinkData> linkCalls;
            linkCalls.resize(linkCallCount);
            readData((char *)linkCalls.data(), linkCallCount * sizeof(QV4::JIT::CachedLinkData), stream);

            quint32 constVectorLen = 0;
            readData((char *)&constVectorLen, sizeof(quint32), stream);

            QVector<QV4::Primitive > constantVector;
            if (constVectorLen > 0) {
                constantVector.resize(constVectorLen);
                readData((char *)constantVector.data(), constVectorLen * sizeof(QV4::Primitive), stream);
            }

            // Pre-allocate link buffer to append code
            QV4::ExecutableAllocator* executableAllocator = m_engine->v4engine()->executableAllocator;

            QV4::IR::Function nullFunction(0, 0, QLatin1String(""));

            QV4::JIT::Assembler* as = new QV4::JIT::Assembler(this->impl(), &nullFunction, executableAllocator);

            QList<QV4::JIT::Assembler::CallToLink>& callsToLink = as->callsToLink();
            for (int i = 0; i < linkCalls.size(); i++) {
                QV4::JIT::CachedLinkData& call = linkCalls[i];
                void *functionPtr = CACHED_LINK_TABLE[call.index].addr;
                QV4::JIT::Assembler::CallToLink c;
                JSC::AssemblerLabel label(call.offset);
                c.call = QV4::JIT::Assembler::Call(label, QV4::JIT::Assembler::Call::Linkable);
                c.externalFunction = JSC::FunctionPtr((quint32(*)(void))functionPtr);
                c.functionName = CACHED_LINK_TABLE[call.index].name;
                callsToLink.append(c);
            }

            QV4::JIT::Assembler::ConstantTable& constTable = as->constantTable();
            foreach (const QV4::Primitive &p, constantVector)
               constTable.add(p);

            as->appendData(data, len);

            int dummySize = -1; // Pass known value to trigger use of code buffer
            tmpUnit->codeRefs[i] = as->link(&dummySize);
            //Q_ASSERT(dummySize == (int)codeRefLen);

            delete as;
        }

        quint32 size = 0;
        readData((char *)&size, sizeof(quint32), stream);

        void *dataPtr = malloc(size);
        QV4::CompiledData::Unit *finalUnit = reinterpret_cast<QV4::CompiledData::Unit*>(dataPtr);
        if (size > 0)
            readData((char *)dataPtr, size, stream);

        result = backendCompileStep();
        unit = result.data();

        unit->data = nullptr;
        if (irModule->functions.size() > 0)
            unit->data = finalUnit;
        unit->isRestored = true;
    } else {
        // Not loading from cache, run all instructions
        result = runAll(false);
        unit = result.data();
    }

    if ((unit->data == nullptr) && (do_save || generateUnitData))
        unit->data = jsGenerator->generateUnit();

    // Save compilation unit
    QV4::JIT::CompilationUnit *jitUnit = (QV4::JIT::CompilationUnit *) unit;
    if (!loaded) {
        if (cacheFile.open(QIODevice::WriteOnly)) {
            // TODO: Support inline compilation units
            if (!irModule->fileName.isEmpty() && !irModule->fileName.contains(QLatin1String("inline")) && m_engine) {
                QBuffer fillBuff;
                fillBuff.open(QIODevice::WriteOnly);
                QDataStream stream(&fillBuff);

                quint32 fileNameSize = irModule->fileName.size();
                writeData(stream, (const char *)&fileNameSize, sizeof(quint32));
                writeData(stream, (const char *)irModule->fileName.toLatin1().remove(0, 7).constData(), irModule->fileName.size());

                struct stat sb;
                stat(irModule->fileName.toLatin1().remove(0, 7).constData(), &sb);
                writeData(stream, (const char *)&sb.st_mtime, sizeof(quint32));

                for (int i = 0; i < jitUnit->codeRefs.size(); i++) {
                    const JSC::MacroAssemblerCodeRef &codeRef = jitUnit->codeRefs[i];
                    const QVector<QV4::Primitive> &constantValue = jitUnit->constantValues[i];
                    quint32 len = codeRef.size();

                    QString hashString(QLatin1String(irModule->functions.at(i)->name->toLatin1()));
                    hashString.append(QString::number(irModule->functions.at(i)->line));
                    hashString.append(QString::number(irModule->functions.at(i)->column));

                    quint32 strLen = hashString.size();
                    strLen += 1; // /0 char
                    writeData(stream, (const char *)&strLen, sizeof(quint32));
                    writeData(stream, (const char *)hashString.toLatin1().constData(), strLen);
                    writeData(stream, (const char *)&len, sizeof(quint32));
                    writeData(stream, (const char *)(((unsigned long)codeRef.code().executableAddress())&~1), len);

                    const QVector<QV4::JIT::CachedLinkData> &linkCalls = jitUnit->linkData[i];
                    quint32 linkCallCount = linkCalls.size();

                    writeData(stream, (const char *)&linkCallCount, sizeof(quint32));
                    if (linkCallCount > 0)
                        writeData(stream, (const char *)linkCalls.data(), linkCalls.size() * sizeof (QV4::JIT::CachedLinkData));

                    quint32 constTableCount = constantValue.size();
                    writeData(stream, (const char *)&constTableCount, sizeof(quint32));

                    if (constTableCount > 0)
                        writeData(stream, (const char*)constantValue.data(), sizeof(QV4::Primitive) * constantValue.size());
                }

                QV4::CompiledData::Unit *retUnit = unit->data;
                quint32 size = retUnit->unitSize;

                if (size > 0) {
                    writeData(stream, (const char*)&size, sizeof(quint32));
                    writeData(stream, (const char*)retUnit, size);
                }

                // Write MD5 hash of stored data for consistency check
                QByteArray fileHash(QCryptographicHash::hash(fillBuff.data(), QCryptographicHash::Md5).toHex());
                fillBuff.write(fileHash);
                cacheFile.write(fillBuff.data());
            }
            cacheFile.close();
        } else {
            cacheFile.close();
        }
    }

    return result;
#endif
}

void IRDecoder::visitMove(IR::Move *s)
{
    if (IR::Name *n = s->target->asName()) {
        if (s->source->asTemp() || s->source->asConst() || s->source->asArgLocal()) {
            setActivationProperty(s->source, *n->id);
            return;
        }
    } else if (s->target->asTemp() || s->target->asArgLocal()) {
        if (IR::Name *n = s->source->asName()) {
            if (n->id && *n->id == QStringLiteral("this")) // TODO: `this' should be a builtin.
                loadThisObject(s->target);
            else if (n->builtin == IR::Name::builtin_qml_context)
                loadQmlContext(s->target);
            else if (n->builtin == IR::Name::builtin_qml_imported_scripts_object)
                loadQmlImportedScripts(s->target);
            else if (n->qmlSingleton)
                loadQmlSingleton(*n->id, s->target);
            else
                getActivationProperty(n, s->target);
            return;
        } else if (IR::Const *c = s->source->asConst()) {
            loadConst(c, s->target);
            return;
        } else if (s->source->asTemp() || s->source->asArgLocal()) {
            if (s->swap)
                swapValues(s->source, s->target);
            else
                copyValue(s->source, s->target);
            return;
        } else if (IR::String *str = s->source->asString()) {
            loadString(*str->value, s->target);
            return;
        } else if (IR::RegExp *re = s->source->asRegExp()) {
            loadRegexp(re, s->target);
            return;
        } else if (IR::Closure *clos = s->source->asClosure()) {
            initClosure(clos, s->target);
            return;
        } else if (IR::New *ctor = s->source->asNew()) {
            if (Name *func = ctor->base->asName()) {
                constructActivationProperty(func, ctor->args, s->target);
                return;
            } else if (IR::Member *member = ctor->base->asMember()) {
                constructProperty(member->base, *member->name, ctor->args, s->target);
                return;
            } else if (ctor->base->asTemp() || ctor->base->asArgLocal()) {
                constructValue(ctor->base, ctor->args, s->target);
                return;
            }
        } else if (IR::Member *m = s->source->asMember()) {
            if (m->property) {
#ifdef V4_BOOTSTRAP
                Q_UNIMPLEMENTED();
#else
                bool captureRequired = true;

                Q_ASSERT(m->kind != IR::Member::MemberOfEnum && m->kind != IR::Member::MemberOfIdObjectsArray);
                const int attachedPropertiesId = m->attachedPropertiesId;
                const bool isSingletonProperty = m->kind == IR::Member::MemberOfSingletonObject;

                if (_function && attachedPropertiesId == 0 && !m->property->isConstant()) {
                    if (m->kind == IR::Member::MemberOfQmlContextObject) {
                        _function->contextObjectPropertyDependencies.insert(m->property->coreIndex, m->property->notifyIndex);
                        captureRequired = false;
                    } else if (m->kind == IR::Member::MemberOfQmlScopeObject) {
                        _function->scopeObjectPropertyDependencies.insert(m->property->coreIndex, m->property->notifyIndex);
                        captureRequired = false;
                    }
                }
                if (m->kind == IR::Member::MemberOfQmlScopeObject || m->kind == IR::Member::MemberOfQmlContextObject) {
                    getQmlContextProperty(m->base, (IR::Member::MemberKind)m->kind, m->property->coreIndex, s->target);
                    return;
                }
                getQObjectProperty(m->base, m->property->coreIndex, captureRequired, isSingletonProperty, attachedPropertiesId, s->target);
#endif // V4_BOOTSTRAP
                return;
            } else if (m->kind == IR::Member::MemberOfIdObjectsArray) {
                getQmlContextProperty(m->base, (IR::Member::MemberKind)m->kind, m->idIndex, s->target);
                return;
            } else if (m->base->asTemp() || m->base->asConst() || m->base->asArgLocal()) {
                getProperty(m->base, *m->name, s->target);
                return;
            }
        } else if (IR::Subscript *ss = s->source->asSubscript()) {
            getElement(ss->base, ss->index, s->target);
            return;
        } else if (IR::Unop *u = s->source->asUnop()) {
            unop(u->op, u->expr, s->target);
            return;
        } else if (IR::Binop *b = s->source->asBinop()) {
            binop(b->op, b->left, b->right, s->target);
            return;
        } else if (IR::Call *c = s->source->asCall()) {
            if (c->base->asName()) {
                callBuiltin(c, s->target);
                return;
            } else if (Member *member = c->base->asMember()) {
#ifndef V4_BOOTSTRAP
                Q_ASSERT(member->kind != IR::Member::MemberOfIdObjectsArray);
                if (member->kind == IR::Member::MemberOfQmlScopeObject || member->kind == IR::Member::MemberOfQmlContextObject) {
                    callQmlContextProperty(member->base, (IR::Member::MemberKind)member->kind, member->property->coreIndex, c->args, s->target);
                    return;
                }
#endif
                callProperty(member->base, *member->name, c->args, s->target);
                return;
            } else if (Subscript *ss = c->base->asSubscript()) {
                callSubscript(ss->base, ss->index, c->args, s->target);
                return;
            } else if (c->base->asTemp() || c->base->asArgLocal() || c->base->asConst()) {
                callValue(c->base, c->args, s->target);
                return;
            }
        } else if (IR::Convert *c = s->source->asConvert()) {
            Q_ASSERT(c->expr->asTemp() || c->expr->asArgLocal());
            convertType(c->expr, s->target);
            return;
        }
    } else if (IR::Member *m = s->target->asMember()) {
        if (m->base->asTemp() || m->base->asConst() || m->base->asArgLocal()) {
            if (s->source->asTemp() || s->source->asConst() || s->source->asArgLocal()) {
                Q_ASSERT(m->kind != IR::Member::MemberOfEnum);
                Q_ASSERT(m->kind != IR::Member::MemberOfIdObjectsArray);
                const int attachedPropertiesId = m->attachedPropertiesId;
                if (m->property && attachedPropertiesId == 0) {
#ifdef V4_BOOTSTRAP
                    Q_UNIMPLEMENTED();
#else
                    if (m->kind == IR::Member::MemberOfQmlScopeObject || m->kind == IR::Member::MemberOfQmlContextObject) {
                        setQmlContextProperty(s->source, m->base, (IR::Member::MemberKind)m->kind, m->property->coreIndex);
                        return;
                    }
                    setQObjectProperty(s->source, m->base, m->property->coreIndex);
#endif
                    return;
                } else {
                    setProperty(s->source, m->base, *m->name);
                    return;
                }
            }
        }
    } else if (IR::Subscript *ss = s->target->asSubscript()) {
        if (s->source->asTemp() || s->source->asConst() || s->source->asArgLocal()) {
            setElement(s->source, ss->base, ss->index);
            return;
        }
    }

    // For anything else...:
    Q_UNIMPLEMENTED();
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QTextStream qout(&buf);
    IRPrinter(&qout).print(s);
    qout << endl;
    qDebug("%s", buf.data().constData());
    Q_ASSERT(!"TODO");
}

IRDecoder::~IRDecoder()
{
}

void IRDecoder::visitExp(IR::Exp *s)
{
    if (IR::Call *c = s->expr->asCall()) {
        // These are calls where the result is ignored.
        if (c->base->asName()) {
            callBuiltin(c, 0);
        } else if (c->base->asTemp() || c->base->asArgLocal() || c->base->asConst()) {
            callValue(c->base, c->args, 0);
        } else if (Member *member = c->base->asMember()) {
            Q_ASSERT(member->base->asTemp() || member->base->asArgLocal());
#ifndef V4_BOOTSTRAP
            Q_ASSERT(member->kind != IR::Member::MemberOfIdObjectsArray);
            if (member->kind == IR::Member::MemberOfQmlScopeObject || member->kind == IR::Member::MemberOfQmlContextObject) {
                callQmlContextProperty(member->base, (IR::Member::MemberKind)member->kind, member->property->coreIndex, c->args, 0);
                return;
            }
#endif
            callProperty(member->base, *member->name, c->args, 0);
        } else if (Subscript *s = c->base->asSubscript()) {
            callSubscript(s->base, s->index, c->args, 0);
        } else {
            Q_UNREACHABLE();
        }
    } else {
        Q_UNREACHABLE();
    }
}

void IRDecoder::callBuiltin(IR::Call *call, Expr *result)
{
    IR::Name *baseName = call->base->asName();
    Q_ASSERT(baseName != 0);

    switch (baseName->builtin) {
    case IR::Name::builtin_invalid:
        callBuiltinInvalid(baseName, call->args, result);
        return;

    case IR::Name::builtin_typeof: {
        if (IR::Member *member = call->args->expr->asMember()) {
#ifndef V4_BOOTSTRAP
            Q_ASSERT(member->kind != IR::Member::MemberOfIdObjectsArray);
            if (member->kind == IR::Member::MemberOfQmlScopeObject || member->kind == IR::Member::MemberOfQmlContextObject) {
                callBuiltinTypeofQmlContextProperty(member->base,
                                                    IR::Member::MemberKind(member->kind),
                                                    member->property->coreIndex, result);
                return;
            }
#endif
            callBuiltinTypeofMember(member->base, *member->name, result);
            return;
        } else if (IR::Subscript *ss = call->args->expr->asSubscript()) {
            callBuiltinTypeofSubscript(ss->base, ss->index, result);
            return;
        } else if (IR::Name *n = call->args->expr->asName()) {
            callBuiltinTypeofName(*n->id, result);
            return;
        } else if (call->args->expr->asTemp() ||
                   call->args->expr->asConst() ||
                   call->args->expr->asArgLocal()) {
            callBuiltinTypeofValue(call->args->expr, result);
            return;
        }
    } break;

    case IR::Name::builtin_delete: {
        if (IR::Member *m = call->args->expr->asMember()) {
            callBuiltinDeleteMember(m->base, *m->name, result);
            return;
        } else if (IR::Subscript *ss = call->args->expr->asSubscript()) {
            callBuiltinDeleteSubscript(ss->base, ss->index, result);
            return;
        } else if (IR::Name *n = call->args->expr->asName()) {
            callBuiltinDeleteName(*n->id, result);
            return;
        } else if (call->args->expr->asTemp() ||
                   call->args->expr->asArgLocal()) {
            // TODO: should throw in strict mode
            callBuiltinDeleteValue(result);
            return;
        }
    } break;

    case IR::Name::builtin_throw: {
        IR::Expr *arg = call->args->expr;
        Q_ASSERT(arg->asTemp() || arg->asConst() || arg->asArgLocal());
        callBuiltinThrow(arg);
    } return;

    case IR::Name::builtin_rethrow: {
        callBuiltinReThrow();
    } return;

    case IR::Name::builtin_unwind_exception: {
        callBuiltinUnwindException(result);
    } return;

    case IR::Name::builtin_push_catch_scope: {
        IR::String *s = call->args->expr->asString();
        Q_ASSERT(s);
        callBuiltinPushCatchScope(*s->value);
    } return;

    case IR::Name::builtin_foreach_iterator_object: {
        IR::Expr *arg = call->args->expr;
        Q_ASSERT(arg != 0);
        callBuiltinForeachIteratorObject(arg, result);
    } return;

    case IR::Name::builtin_foreach_next_property_name: {
        IR::Expr *arg = call->args->expr;
        Q_ASSERT(arg != 0);
        callBuiltinForeachNextPropertyname(arg, result);
    } return;
    case IR::Name::builtin_push_with_scope: {
        if (call->args->expr->asTemp() || call->args->expr->asArgLocal())
            callBuiltinPushWithScope(call->args->expr);
        else
            Q_UNIMPLEMENTED();
    } return;

    case IR::Name::builtin_pop_scope:
        callBuiltinPopScope();
        return;

    case IR::Name::builtin_declare_vars: {
        if (!call->args)
            return;
        IR::Const *deletable = call->args->expr->asConst();
        Q_ASSERT(deletable->type == IR::BoolType);
        for (IR::ExprList *it = call->args->next; it; it = it->next) {
            IR::Name *arg = it->expr->asName();
            Q_ASSERT(arg != 0);
            callBuiltinDeclareVar(deletable->value != 0, *arg->id);
        }
    } return;

    case IR::Name::builtin_define_array:
        callBuiltinDefineArray(result, call->args);
        return;

    case IR::Name::builtin_define_object_literal: {
        IR::ExprList *args = call->args;
        const int keyValuePairsCount = args->expr->asConst()->value;
        args = args->next;

        IR::ExprList *keyValuePairs = args;
        for (int i = 0; i < keyValuePairsCount; ++i) {
            args = args->next; // name
            bool isData = args->expr->asConst()->value;
            args = args->next; // isData flag
            args = args->next; // value or getter
            if (!isData)
                args = args->next; // setter
        }

        IR::ExprList *arrayEntries = args;
        bool needSparseArray = false;
        for (IR::ExprList *it = arrayEntries; it; it = it->next) {
            uint index = it->expr->asConst()->value;
            if (index > 16)  {
                needSparseArray = true;
                break;
            }
            it = it->next;
            bool isData = it->expr->asConst()->value;
            it = it->next;
            if (!isData)
                it = it->next;
        }

        callBuiltinDefineObjectLiteral(result, keyValuePairsCount, keyValuePairs, arrayEntries, needSparseArray);
    } return;

    case IR::Name::builtin_setup_argument_object:
        callBuiltinSetupArgumentObject(result);
        return;

    case IR::Name::builtin_convert_this_to_object:
        callBuiltinConvertThisToObject();
        return;

    default:
        break;
    }

    Q_UNIMPLEMENTED();
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QTextStream qout(&buf);
    IRPrinter(&qout).print(call); qout << endl;
    qDebug("%s", buf.data().constData());
    Q_UNREACHABLE();
}
