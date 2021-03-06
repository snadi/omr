/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 2000, 2016
 *
 *  This program and the accompanying materials are made available
 *  under the terms of the Eclipse Public License v1.0 and
 *  Apache License v2.0 which accompanies this distribution.
 *
 *      The Eclipse Public License is available at
 *      http://www.eclipse.org/legal/epl-v10.html
 *
 *      The Apache License v2.0 is available at
 *      http://www.opensource.org/licenses/apache2.0.php
 *
 * Contributors:
 *    Multiple authors (IBM Corp.) - initial implementation and documentation
 *******************************************************************************/

#include "codegen/OutOfLineCodeSection.hpp"

#include <stddef.h>                         // for NULL
#include <stdint.h>                         // for int32_t
#include "codegen/CodeGenerator.hpp"        // for CodeGenerator
#include "codegen/TreeEvaluator.hpp"        // for TreeEvaluator
#include "compile/Compilation.hpp"          // for Compilation
#include "il/ILOps.hpp"                     // for ILOpCode
#include "il/Node.hpp"                      // for Node, vcount_t
#include "il/Node_inlines.hpp"              // for Node::setReferenceCount, etc
#include "il/Symbol.hpp"                    // for Symbol
#include "il/SymbolReference.hpp"           // for SymbolReference
#include "il/symbol/LabelSymbol.hpp"        // for LabelSymbol
#include "infra/Assert.hpp"                 // for TR_ASSERT

TR_OutOfLineCodeSection::TR_OutOfLineCodeSection(TR::Node          *callNode,
                                                 TR::ILOpCodes      callOp,
                                                 TR::Register      *targetReg,
                                                 TR::LabelSymbol    *entryLabel,
                                                 TR::LabelSymbol    *restartLabel,
                                                 TR::CodeGenerator *cg) :
   _restartLabel(restartLabel),
   _firstInstruction(NULL),
   _appendInstruction(NULL),
   _targetReg(targetReg),
   _entryLabel(entryLabel),
   _hasBeenRegisterAssigned(false),
   _cg(cg)
   {
   _entryLabel->setStartOfColdInstructionStream();
   _restartLabel->setEndOfColdInstructionStream();
   _block = callNode->getSymbolReference()->canCauseGC() ? cg->getCurrentEvaluationBlock() : 0;

   _callNode = createOutOfLineCallNode(callNode, callOp);
   }

TR_OutOfLineCodeSection::TR_OutOfLineCodeSection(TR::LabelSymbol *entryLabel, TR::CodeGenerator *cg) :
   _entryLabel(entryLabel),
   _cg(cg),
   _firstInstruction(NULL),
   _appendInstruction(NULL),
   _restartLabel(NULL),
   _block(NULL),
   _callNode(NULL),
   _targetReg(NULL),
   _hasBeenRegisterAssigned(false)
   {
   _entryLabel->setStartOfColdInstructionStream();
   }

TR_OutOfLineCodeSection::TR_OutOfLineCodeSection(TR::LabelSymbol *entryLabel, TR::LabelSymbol *restartLabel, TR::CodeGenerator *cg) :
   _entryLabel(entryLabel),
   _cg(cg),
   _firstInstruction(NULL),
   _appendInstruction(NULL),
   _restartLabel(restartLabel),
   _block(NULL),
   _callNode(NULL),
   _targetReg(NULL),
   _hasBeenRegisterAssigned(false)
   {
   _entryLabel->setStartOfColdInstructionStream();
   _restartLabel->setEndOfColdInstructionStream();
   _block = cg->getCurrentEvaluationBlock();
   }

void TR_OutOfLineCodeSection::swapInstructionListsWithCompilation()
   {
   TR::Instruction *temp;
   TR::Compilation *comp = _cg->comp();

   temp = comp->getFirstInstruction();
   comp->setFirstInstruction(_firstInstruction);
   _firstInstruction = temp;

   temp = comp->getAppendInstruction();
   comp->setAppendInstruction(_appendInstruction);
   _appendInstruction = temp;
   _cg->toggleIsInOOLSection();
   }

// Evaluate every subtree S of a node N that meets the following criteria:
//
//    (1) the first reference to S is in a subtree of N, and
//    (2) not all references to S appear under N
//
// All subtrees will be evaluated as soon as they are discovered.
//
void TR_OutOfLineCodeSection::preEvaluatePersistentHelperArguments()
   {
   TR_ASSERT(_callNode, "preEvaluatePersistentHelperArguments can only be called for TR_OutOfLineCodeSection which are helper calls");
   TR::TreeEvaluator::initializeStrictlyFutureUseCounts(_callNode, _cg->comp()->incVisitCount(), _cg);
   evaluateNodesWithFutureUses(_callNode);
   }

void TR_OutOfLineCodeSection::evaluateNodesWithFutureUses(TR::Node *node)
   {
   if (node->getRegister() != NULL)
      {
      // Node has already been evaluated outside this tree.
      //
      return;
      }

   if (node->getFutureUseCount() > 0)
      {
      (void)_cg->evaluate(node);

      // Do not decrement the reference count here.  It will be decremented when the call node is evaluated
      // again in the helper instruction stream.
      //

      return;
      }

   for (int32_t i = 0; i<node->getNumChildren(); i++)
      evaluateNodesWithFutureUses(node->getChild(i));
   }

TR::Node *TR_OutOfLineCodeSection::createOutOfLineCallNode(TR::Node *callNode, TR::ILOpCodes callOp)
   {
   int32_t   i;
   vcount_t  visitCount = _cg->comp()->incVisitCount();
   TR::Node  *child;

   for (i=0; i<callNode->getNumChildren(); i++)
      {
      child = callNode->getChild(i);
      TR::TreeEvaluator::initializeStrictlyFutureUseCounts(child, visitCount, _cg);
      }

   TR::Node *newCallNode = TR::Node::createWithSymRef(callNode, callOp, callNode->getNumChildren(), callNode->getSymbolReference());
   newCallNode->setReferenceCount(1);

   for (i=0; i<callNode->getNumChildren(); i++)
      {
      child = callNode->getChild(i);

      if (child->getRegister() != NULL)
         {
         // Child has already been evaluated outside this tree.
         //
         newCallNode->setAndIncChild(i, child);
         }
      else if (child->getOpCode().isLoadConst())
         {
         // Copy unevaluated constant nodes.
         //
         child = TR::Node::copy(child);
         child->setReferenceCount(1);
         newCallNode->setChild(i, child);
         }
      else
         {
         if ((child->getOpCodeValue() == TR::loadaddr)                                                       &&
             (callNode->getOpCodeValue() == TR::instanceof || callNode->getOpCodeValue() == TR::checkcast || callNode->getOpCodeValue() == TR::checkcastAndNULLCHK)    &&
             (child->getSymbolReference()->getSymbol())                                                     &&
             (child->getSymbolReference()->getSymbol()->getStaticSymbol()))
            {
            child = TR::Node::copy(child);
            child->setReferenceCount(1);
            newCallNode->setChild(i, child);
            }
         else
            {
            // Be very conservative at this point, even though it is possible to make it less so.  For example, this will catch
            // the case of an unevaluated argument not persisting outside of the outlined region even though one of its subtrees will.
            //
            (void)_cg->evaluate(child);

            // Do not decrement the reference count here.  It will be decremented when the call node is evaluated
            // again in the helper instruction stream.
            //
            newCallNode->setAndIncChild(i, child);
            }
         }
      }

   return newCallNode;
   }
