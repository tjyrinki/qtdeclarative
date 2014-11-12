/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
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
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QString>
#include "qv4debugging_p.h"
#include <qv4context_p.h>
#include <qv4object_p.h>
#include <qv4objectproto_p.h>
#include "qv4mm_p.h"
#include <qv4argumentsobject_p.h>
#include "qv4function_p.h"
#include "qv4errorobject_p.h"

using namespace QV4;

DEFINE_MANAGED_VTABLE(ExecutionContext);
DEFINE_MANAGED_VTABLE(CallContext);
DEFINE_MANAGED_VTABLE(WithContext);
DEFINE_MANAGED_VTABLE(GlobalContext);

Heap::CallContext *ExecutionContext::newCallContext(FunctionObject *function, CallData *callData)
{
    Q_ASSERT(function->function());

    Heap::CallContext *c = reinterpret_cast<Heap::CallContext *>(d()->engine->memoryManager->allocManaged(requiredMemoryForExecutionContect(function, callData->argc)));
    new (c) Heap::CallContext(d()->engine, Heap::ExecutionContext::Type_CallContext);

    c->function = function->d();
    c->realArgumentCount = callData->argc;

    c->strictMode = function->strictMode();
    c->outer = function->scope();

    c->activation = 0;

    c->compilationUnit = function->function()->compilationUnit;
    c->lookups = c->compilationUnit->runtimeLookups;
    c->locals = (Value *)((quintptr(c + 1) + 7) & ~7);

    const CompiledData::Function *compiledFunction = function->function()->compiledFunction;
    int nLocals = compiledFunction->nLocals;
    if (nLocals)
        std::fill(c->locals, c->locals + nLocals, Primitive::undefinedValue());

    c->callData = reinterpret_cast<CallData *>(c->locals + nLocals);
    ::memcpy(c->callData, callData, sizeof(CallData) + (callData->argc - 1) * sizeof(Value));
    if (callData->argc < static_cast<int>(compiledFunction->nFormals))
        std::fill(c->callData->args + c->callData->argc, c->callData->args + compiledFunction->nFormals, Primitive::undefinedValue());
    c->callData->argc = qMax((uint)callData->argc, compiledFunction->nFormals);

    return c;
}

Heap::WithContext *ExecutionContext::newWithContext(Object *with)
{
    return d()->engine->memoryManager->alloc<WithContext>(d()->engine, with);
}

Heap::CatchContext *ExecutionContext::newCatchContext(String *exceptionVarName, const ValueRef exceptionValue)
{
    return d()->engine->memoryManager->alloc<CatchContext>(d()->engine, exceptionVarName, exceptionValue);
}

Heap::CallContext *ExecutionContext::newQmlContext(FunctionObject *f, Object *qml)
{
    Scope scope(this);
    Scoped<CallContext> c(scope, static_cast<CallContext*>(d()->engine->memoryManager->allocManaged(requiredMemoryForExecutionContect(f, 0))));
    new (c->d()) Heap::CallContext(d()->engine, qml, f);
    return c->d();
}



void ExecutionContext::createMutableBinding(String *name, bool deletable)
{
    Scope scope(this);

    // find the right context to create the binding on
    ScopedObject activation(scope, d()->engine->globalObject);
    Scoped<ExecutionContext> ctx(scope, this);
    while (ctx) {
        if (ctx->d()->type >= Heap::ExecutionContext::Type_CallContext) {
            CallContext *c = static_cast<CallContext *>(ctx.getPointer());
            if (!c->d()->activation)
                c->d()->activation = d()->engine->newObject();
            activation = c->d()->activation;
            break;
        }
        ctx = ctx->d()->outer;
    }

    if (activation->hasProperty(name))
        return;
    Property desc(Primitive::undefinedValue());
    PropertyAttributes attrs(Attr_Data);
    attrs.setConfigurable(deletable);
    activation->__defineOwnProperty__(scope.engine, name, desc, attrs);
}


Heap::GlobalContext::GlobalContext(ExecutionEngine *eng)
    : Heap::ExecutionContext(eng, Heap::ExecutionContext::Type_GlobalContext)
{
    global = eng->globalObject->d();
}

Heap::WithContext::WithContext(ExecutionEngine *engine, QV4::Object *with)
    : Heap::ExecutionContext(engine, Heap::ExecutionContext::Type_WithContext)
{
    callData = parent->callData;
    outer = parent;
    lookups = parent->lookups;
    compilationUnit = parent->compilationUnit;

    withObject = with->d();
}

Heap::CatchContext::CatchContext(ExecutionEngine *engine, QV4::String *exceptionVarName, const ValueRef exceptionValue)
    : Heap::ExecutionContext(engine, Heap::ExecutionContext::Type_CatchContext)
{
    strictMode = parent->strictMode;
    callData = parent->callData;
    outer = parent;
    lookups = parent->lookups;
    compilationUnit = parent->compilationUnit;

    this->exceptionVarName = exceptionVarName;
    this->exceptionValue = exceptionValue;
}

Heap::CallContext::CallContext(ExecutionEngine *engine, QV4::Object *qml, QV4::FunctionObject *function)
    : Heap::ExecutionContext(engine, Heap::ExecutionContext::Type_QmlContext)
{
    this->function = function->d();
    callData = reinterpret_cast<CallData *>(this + 1);
    callData->tag = QV4::Value::_Integer_Type;
    callData->argc = 0;
    callData->thisObject = Primitive::undefinedValue();

    strictMode = true;
    outer = function->scope();

    activation = qml->d();

    if (function->function()) {
        compilationUnit = function->function()->compilationUnit;
        lookups = compilationUnit->runtimeLookups;
    }

    locals = (Value *)(this + 1);
    if (function->varCount())
        std::fill(locals, locals + function->varCount(), Primitive::undefinedValue());
}

String * const *CallContext::formals() const
{
    return (d()->function && d()->function->function) ? d()->function->function->internalClass->nameMap.constData() : 0;
}

unsigned int CallContext::formalCount() const
{
    return d()->function ? d()->function->formalParameterCount() : 0;
}

String * const *CallContext::variables() const
{
    return (d()->function && d()->function->function) ? d()->function->function->internalClass->nameMap.constData() + d()->function->formalParameterCount() : 0;
}

unsigned int CallContext::variableCount() const
{
    return d()->function ? d()->function->varCount() : 0;
}



bool ExecutionContext::deleteProperty(String *name)
{
    Scope scope(this);
    bool hasWith = false;
    Scoped<ExecutionContext> ctx(scope, this);
    for (; ctx; ctx = ctx->d()->outer) {
        if (ctx->d()->type == Heap::ExecutionContext::Type_WithContext) {
            hasWith = true;
            ScopedObject withObject(scope, static_cast<WithContext *>(ctx.getPointer())->d()->withObject);
            if (withObject->hasProperty(name))
                return withObject->deleteProperty(name);
        } else if (ctx->d()->type == Heap::ExecutionContext::Type_CatchContext) {
            CatchContext *c = static_cast<CatchContext *>(ctx.getPointer());
            if (c->d()->exceptionVarName->isEqualTo(name))
                return false;
        } else if (ctx->d()->type >= Heap::ExecutionContext::Type_CallContext) {
            CallContext *c = static_cast<CallContext *>(ctx.getPointer());
            ScopedFunctionObject f(scope, c->d()->function);
            if (f->needsActivation() || hasWith) {
                uint index = f->function()->internalClass->find(name);
                if (index < UINT_MAX)
                    // ### throw in strict mode?
                    return false;
            }
            ScopedObject activation(scope, c->d()->activation);
            if (activation && activation->hasProperty(name))
                return activation->deleteProperty(name);
        } else if (ctx->d()->type == Heap::ExecutionContext::Type_GlobalContext) {
            ScopedObject global(scope, static_cast<GlobalContext *>(ctx.getPointer())->d()->global);
            if (global->hasProperty(name))
                return global->deleteProperty(name);
        }
    }

    if (d()->strictMode)
        engine()->throwSyntaxError(QStringLiteral("Can't delete property %1").arg(name->toQString()));
    return true;
}

bool CallContext::needsOwnArguments() const
{
    return d()->function->needsActivation || d()->callData->argc < static_cast<int>(d()->function->formalParameterCount());
}

void ExecutionContext::markObjects(Heap::Base *m, ExecutionEngine *engine)
{
    ExecutionContext::Data *ctx = static_cast<ExecutionContext::Data *>(m);

    if (ctx->outer)
        ctx->outer->mark(engine);

    // ### shouldn't need these 3 lines
    ctx->callData->thisObject.mark(engine);
    for (int arg = 0; arg < ctx->callData->argc; ++arg)
        ctx->callData->args[arg].mark(engine);

    if (ctx->type >= Heap::ExecutionContext::Type_CallContext) {
        QV4::Heap::CallContext *c = static_cast<Heap::CallContext *>(ctx);
        for (unsigned local = 0, lastLocal = c->function->varCount(); local < lastLocal; ++local)
            c->locals[local].mark(engine);
        if (c->activation)
            c->activation->mark(engine);
        c->function->mark(engine);
    } else if (ctx->type == Heap::ExecutionContext::Type_WithContext) {
        WithContext::Data *w = static_cast<WithContext::Data *>(ctx);
        w->withObject->mark(engine);
    } else if (ctx->type == Heap::ExecutionContext::Type_CatchContext) {
        CatchContext::Data *c = static_cast<CatchContext::Data *>(ctx);
        c->exceptionVarName->mark(engine);
        c->exceptionValue.mark(engine);
    } else if (ctx->type == Heap::ExecutionContext::Type_GlobalContext) {
        GlobalContext::Data *g = static_cast<GlobalContext::Data *>(ctx);
        g->global->mark(engine);
    }
}

void ExecutionContext::setProperty(String *name, const ValueRef value)
{
    Scope scope(this);
    Scoped<ExecutionContext> ctx(scope, this);
    for (; ctx; ctx = ctx->d()->outer) {
        if (ctx->d()->type == Heap::ExecutionContext::Type_WithContext) {
            ScopedObject w(scope, static_cast<WithContext *>(ctx.getPointer())->d()->withObject);
            if (w->hasProperty(name)) {
                w->put(name, value);
                return;
            }
        } else if (ctx->d()->type == Heap::ExecutionContext::Type_CatchContext && static_cast<CatchContext *>(ctx.getPointer())->d()->exceptionVarName->isEqualTo(name)) {
            static_cast<CatchContext *>(ctx.getPointer())->d()->exceptionValue = *value;
            return;
        } else {
            ScopedObject activation(scope, (Object *)0);
            if (ctx->d()->type >= Heap::ExecutionContext::Type_CallContext) {
                CallContext *c = static_cast<CallContext *>(ctx.getPointer());
                if (c->d()->function->function) {
                    uint index = c->d()->function->function->internalClass->find(name);
                    if (index < UINT_MAX) {
                        if (index < c->d()->function->formalParameterCount()) {
                            c->d()->callData->args[c->d()->function->formalParameterCount() - index - 1] = *value;
                        } else {
                            index -= c->d()->function->formalParameterCount();
                            c->d()->locals[index] = *value;
                        }
                        return;
                    }
                }
                activation = c->d()->activation;
            } else if (ctx->d()->type == Heap::ExecutionContext::Type_GlobalContext) {
                activation = static_cast<GlobalContext *>(ctx.getPointer())->d()->global;
            }

            if (activation) {
                if (ctx->d()->type == Heap::ExecutionContext::Type_QmlContext) {
                    activation->put(name, value);
                    return;
                } else {
                    uint member = activation->internalClass()->find(name);
                    if (member < UINT_MAX) {
                        activation->putValue(activation->propertyAt(member), activation->internalClass()->propertyData[member], value);
                        return;
                    }
                }
            }
        }
    }
    if (d()->strictMode || name->equals(d()->engine->id_this.getPointer())) {
        ScopedValue n(scope, name->asReturnedValue());
        engine()->throwReferenceError(n);
        return;
    }
    d()->engine->globalObject->put(name, value);
}

ReturnedValue ExecutionContext::getProperty(String *name)
{
    Scope scope(this);
    ScopedValue v(scope);
    name->makeIdentifier();

    if (name->equals(d()->engine->id_this.getPointer()))
        return d()->callData->thisObject.asReturnedValue();

    bool hasWith = false;
    bool hasCatchScope = false;
    Scoped<ExecutionContext> ctx(scope, this);
    for (; ctx; ctx = ctx->d()->outer) {
        if (ctx->d()->type == Heap::ExecutionContext::Type_WithContext) {
            ScopedObject w(scope, static_cast<WithContext *>(ctx.getPointer())->d()->withObject);
            hasWith = true;
            bool hasProperty = false;
            v = w->get(name, &hasProperty);
            if (hasProperty) {
                return v.asReturnedValue();
            }
            continue;
        }

        else if (ctx->d()->type == Heap::ExecutionContext::Type_CatchContext) {
            hasCatchScope = true;
            CatchContext *c = static_cast<CatchContext *>(ctx.getPointer());
            if (c->d()->exceptionVarName->isEqualTo(name))
                return c->d()->exceptionValue.asReturnedValue();
        }

        else if (ctx->d()->type >= Heap::ExecutionContext::Type_CallContext) {
            QV4::CallContext *c = static_cast<CallContext *>(ctx.getPointer());
            ScopedFunctionObject f(scope, c->d()->function);
            if (f->function() && (f->needsActivation() || hasWith || hasCatchScope)) {
                uint index = f->function()->internalClass->find(name);
                if (index < UINT_MAX) {
                    if (index < c->d()->function->formalParameterCount())
                        return c->d()->callData->args[c->d()->function->formalParameterCount() - index - 1].asReturnedValue();
                    return c->d()->locals[index - c->d()->function->formalParameterCount()].asReturnedValue();
                }
            }
            ScopedObject activation(scope, c->d()->activation);
            if (activation) {
                bool hasProperty = false;
                v = activation->get(name, &hasProperty);
                if (hasProperty)
                    return v.asReturnedValue();
            }
            if (f->function() && f->function()->isNamedExpression()
                && name->equals(ScopedString(scope, f->function()->name())))
                return f.asReturnedValue();
        }

        else if (ctx->d()->type == Heap::ExecutionContext::Type_GlobalContext) {
            ScopedObject global(scope, static_cast<GlobalContext *>(ctx.getPointer())->d()->global);
            bool hasProperty = false;
            v = global->get(name, &hasProperty);
            if (hasProperty)
                return v.asReturnedValue();
        }
    }
    ScopedValue n(scope, name);
    return engine()->throwReferenceError(n);
}

ReturnedValue ExecutionContext::getPropertyAndBase(String *name, Object *&base)
{
    Scope scope(this);
    ScopedValue v(scope);
    base = (Object *)0;
    name->makeIdentifier();

    if (name->equals(d()->engine->id_this.getPointer()))
        return d()->callData->thisObject.asReturnedValue();

    bool hasWith = false;
    bool hasCatchScope = false;
    Scoped<ExecutionContext> ctx(scope, this);
    for (; ctx; ctx = ctx->d()->outer) {
        if (ctx->d()->type == Heap::ExecutionContext::Type_WithContext) {
            ScopedObject w(scope, static_cast<WithContext *>(ctx.getPointer())->d()->withObject);
            hasWith = true;
            bool hasProperty = false;
            v = w->get(name, &hasProperty);
            if (hasProperty) {
                base = w;
                return v.asReturnedValue();
            }
            continue;
        }

        else if (ctx->d()->type == Heap::ExecutionContext::Type_CatchContext) {
            hasCatchScope = true;
            CatchContext *c = static_cast<CatchContext *>(ctx.getPointer());
            if (c->d()->exceptionVarName->isEqualTo(name))
                return c->d()->exceptionValue.asReturnedValue();
        }

        else if (ctx->d()->type >= Heap::ExecutionContext::Type_CallContext) {
            QV4::CallContext *c = static_cast<CallContext *>(ctx.getPointer());
            ScopedFunctionObject f(scope, c->d()->function);
            if (f->function() && (f->needsActivation() || hasWith || hasCatchScope)) {
                uint index = f->function()->internalClass->find(name);
                if (index < UINT_MAX) {
                    if (index < f->formalParameterCount())
                        return c->d()->callData->args[f->formalParameterCount() - index - 1].asReturnedValue();
                    return c->d()->locals[index - f->formalParameterCount()].asReturnedValue();
                }
            }
            ScopedObject activation(scope, c->d()->activation);
            if (activation) {
                bool hasProperty = false;
                v = activation->get(name, &hasProperty);
                if (hasProperty) {
                    if (ctx->d()->type == Heap::ExecutionContext::Type_QmlContext)
                        base = activation;
                    return v.asReturnedValue();
                }
            }
            if (f->function() && f->function()->isNamedExpression()
                && name->equals(ScopedString(scope, f->function()->name())))
                return c->d()->function->asReturnedValue();
        }

        else if (ctx->d()->type == Heap::ExecutionContext::Type_GlobalContext) {
            ScopedObject global(scope, static_cast<GlobalContext *>(ctx.getPointer())->d()->global);
            bool hasProperty = false;
            v = global->get(name, &hasProperty);
            if (hasProperty)
                return v.asReturnedValue();
        }
    }
    ScopedValue n(scope, name);
    return engine()->throwReferenceError(n);
}
