/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzQtComponents/Components/StyleManager.h>

#include <QWidget>

namespace AzQtComponents
{
    namespace StyleHelpers
    {
        /* StyleHelpers::repolishWhenPropertyChanges is used to ensure that style sheet rules that
         * depend on the property of a widget are correctly updated when that property changes.
         *
         * For example, given a Widget with a QLabel as a child, and property called 'drawSimple'
         * one can declaratively change the pixmap of the label using the following stylesheet:
         *
         *      Widget QLabel#icon
         *      {
         *          qproperty-pixmap: url(:/normal-pixmap.png);
         *      }
         *
         *      Widget[drawSimple="true"] QLabel#icon
         *      {
         *          qproperty-pixmap: url(:/simple-pixmap.png);
         *      }
         *
         * If the NOTIFY signal of the property change is drawSimpleChanged, make the following call
         * in the Widget constructor:
         *
         *      StyleHelpers::repolishWhenPropertyChanges(this, &Widget::drawSimpleChanged);
         *
         * See Example::Widget in StyleSheetPage.h and ExampleWidget.qss for a working example.
         */
        // Re-evaluates the style sheet rules of a widget, and optionally of every polished descendant (which is slow for big subtrees).
        inline void repolishWidget(QWidget* widget, bool includeChildren = false)
        {
            // Prevent asserts in Unit Tests
            if (!widget || !StyleManager::isInstanced())
            {
                return;
            }

            if (QStyle* styleSheet = StyleManager::styleSheetStyle(widget))
            {
                styleSheet->unpolish(widget);
                styleSheet->polish(widget);
                if (includeChildren)
                {
                    for (QWidget* child : widget->findChildren<QWidget*>())
                    {
                        // A widget that was never polished has no cached rules to refresh.
                        if (child->testAttribute(Qt::WA_WState_Polished))
                        {
                            styleSheet->unpolish(child);
                            styleSheet->polish(child);
                        }
                    }
                }
            }
        }

        // shouldSkip is checked on every signal; when it returns true only the geometry is invalidated, the restyle is skipped.
        template <typename T, typename SkipFn, typename ...Args>
        void repolishWhenPropertyChanges(T* widget, void (T::*signal)(Args...), SkipFn shouldSkip)
        {
            QObject::connect(widget, signal, widget, [widget, shouldSkip]() {
                // Prevent asserts in Unit Tests
                if (!StyleManager::isInstanced())
                {
                    return;
                }

                if (shouldSkip())
                {
                    widget->updateGeometry();
                    return;
                }

                if (QStyle* styleSheet = StyleManager::styleSheetStyle(widget))
                {
                    // Qt6: unpolish() clears the cached render rules; polish() re-evaluates
                    // them with the new property value. Child render rules must be explicitly invalidated.
                    styleSheet->unpolish(widget);
                    styleSheet->polish(widget);
                    for (QWidget* child : widget->template findChildren<QWidget*>())
                    {
                        // A widget that was never polished has no cached rules to refresh.
                        if (!child->testAttribute(Qt::WA_WState_Polished))
                        {
                            continue;
                        }
                        styleSheet->unpolish(child);
                        styleSheet->polish(child);
                    }
                }
            });
        }

        template <typename T, typename ...Args>
        void repolishWhenPropertyChanges(T* widget, void (T::*signal)(Args...))
        {
            repolishWhenPropertyChanges(widget, signal, []() { return false; });
        }

        /* StyleHelpers::findParent<T> is an utility function to find recursively a parent object of
         * type T. If none is found, the function will return a null pointer.
         */
        template <typename T>
        static T* findParent(const QObject* obj)
        {
            if (!obj)
            {
                return nullptr;
            }

            QObject* parent = obj->parent();

            if (auto p = qobject_cast<T*>(parent))
            {
                return p;
            }

            return findParent<T>(parent);
        }

    } // namespace StyleHelpers
} // namespace AzQtComponents
