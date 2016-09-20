/***************************************************************************
**
** Copyright (C) 2014 Nomovok Ltd. All rights reserved.
** Contact: info@nomovok.com
**
** Copyright (C) 2014 Canonical Limited and/or its subsidiary(-ies).
** Contact: ricardo.mendoza@canonical.com
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
****************************************************************************/

#ifndef QV4CACHEDLINKDATA_P_H
#define QV4CACHEDLINKDATA_P_H

#include <qv4jsir_p.h>
#include <qv4isel_masm_p.h>
#include <qv4runtime_p.h>

QT_BEGIN_NAMESPACE

struct CachedLinkEntry {
    const char *name;
    void *addr;
};

#define CACHED_LINK_TABLE_ADD_NS(x) QV4::Runtime::x
#define CACHED_LINK_TABLE_STR(x) "Runtime::" #x
#define CACHED_LINK_TABLE_ENTRY_RUNTIME(x) { (const char *)CACHED_LINK_TABLE_STR(x), (void *)CACHED_LINK_TABLE_ADD_NS(x) }

// table to link objects
// this table can be used to resolve functions, it is id -> object mapping to maximize performance in linking phase
// when adding new calls, add to end of the list to maintain compatibility
const CachedLinkEntry CACHED_LINK_TABLE[] = {
    // call
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callGlobalLookup),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callActivationProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callQmlScopeObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callQmlContextObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callPropertyLookup),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callElement),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(callValue),

    // construct
    CACHED_LINK_TABLE_ENTRY_RUNTIME(constructGlobalLookup),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(constructActivationProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(constructProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(constructPropertyLookup),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(constructValue),

    // set & get
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setActivationProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setElement),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getActivationProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getElement),

    // typeof
    CACHED_LINK_TABLE_ENTRY_RUNTIME(typeofValue),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(typeofName),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(typeofScopeObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(typeofContextObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(typeofMember),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(typeofElement),

    // delete
    CACHED_LINK_TABLE_ENTRY_RUNTIME(deleteElement),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(deleteMember),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(deleteMemberString),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(deleteName),

    // exceptions & scopes
    CACHED_LINK_TABLE_ENTRY_RUNTIME(throwException),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(unwindException),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(pushWithScope),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(pushCatchScope),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(popScope),

    // closures
    CACHED_LINK_TABLE_ENTRY_RUNTIME(closure),

    // function header
    CACHED_LINK_TABLE_ENTRY_RUNTIME(declareVar),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setupArgumentsObject),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(convertThisToObject),

    // literals
    CACHED_LINK_TABLE_ENTRY_RUNTIME(arrayLiteral),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(objectLiteral),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(regexpLiteral),

    // foreach
    CACHED_LINK_TABLE_ENTRY_RUNTIME(foreachIterator),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(foreachNextPropertyName),

    // unary operators
    //typedef ReturnedValue (*UnaryOperation)(const ValueRef);
    {"NOOP", (void *)qt_noop},
    CACHED_LINK_TABLE_ENTRY_RUNTIME(uPlus),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(uMinus),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(uNot),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(complement),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(increment),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(decrement),

    // binary operators
    //typedef ReturnedValue (*BinaryOperation)(const ValueRef left, const ValueRef right);
    {"NOOP", (void *)qt_noop},
    //typedef ReturnedValue (*BinaryOperationContext)(ExecutionContext *ctx, const ValueRef left, const ValueRef right);
    {"NOOP", (void *)qt_noop},

    CACHED_LINK_TABLE_ENTRY_RUNTIME(instanceof),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(in),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(add),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(addString),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(bitOr),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(bitXor),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(bitAnd),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(sub),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(mul),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(div),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(mod),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(shl),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(shr),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(ushr),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(greaterThan),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(lessThan),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(greaterEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(lessEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(equal),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(notEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(strictEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(strictNotEqual),

    // comparisons
    //typedef Bool (*CompareOperation)(const ValueRef left, const ValueRef right);}
    {"NOOP", (void *)qt_noop},
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareGreaterThan),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareLessThan),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareGreaterEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareLessEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareNotEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareStrictEqual),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareStrictNotEqual),

    //typedef Bool (*CompareOperationContext)(ExecutionContext *ctx, const ValueRef left, const ValueRef right);
    {"NOOP", (void *)qt_noop},
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareInstanceof),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(compareIn),

    // conversions
    CACHED_LINK_TABLE_ENTRY_RUNTIME(toBoolean),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(toDouble),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(toInt),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(doubleToInt),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(toUInt),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(doubleToUInt),

    // qml
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlIdObject),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlContext),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlImportedScripts),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlSingleton),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlContextObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlScopeObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlAttachedProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(getQmlQObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setQmlScopeObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setQmlContextObjectProperty),
    CACHED_LINK_TABLE_ENTRY_RUNTIME(setQmlQObjectProperty)
};

QT_END_NAMESPACE

#endif
