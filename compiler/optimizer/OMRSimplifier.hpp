/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 2000, 2017
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

#ifndef OMR_SIMPLIFIER_INCL
#define OMR_SIMPLIFIER_INCL

#include <stddef.h>                           // for NULL
#include <stdint.h>                           // for int32_t, uint32_t, etc
#include "cs2/hashtab.h"                      // for HashTable
#include "env/IO.hpp"                         // for POINTER_PRINTF_FORMAT
#include "env/TRMemory.hpp"                   // for Allocator
#include "il/DataTypes.hpp"                   // for DataTypes, etc
#include "il/ILOpCodes.hpp"                   // for ILOpCodes
#include "il/ILOps.hpp"                       // for ILOpCode
#include "il/Node.hpp"                        // for Node, etc
#include "il/Node_inlines.hpp"                // for Node::getFirstChild, etc
#include "infra/Assert.hpp"                   // for TR_ASSERT
#include "infra/Checklist.hpp"                // for {Node,Block}Checklist
#include "infra/HashTab.hpp"                  // for TR_HashTabInt
#include "optimizer/Optimization.hpp"         // for Optimization
#include "optimizer/OptimizationManager.hpp"  // for OptimizationManager

class TR_RegionStructure;
class TR_UseDefInfo;
class TR_ValueNumberInfo;
namespace TR { class Block; }
namespace TR { class CodeGenerator; }
namespace TR { class Compilation; }
namespace TR { class SymbolReference; }
namespace TR { class TreeTop; }

namespace OMR
{

typedef enum
   {
   ConditionCode0 = 0,
   ConditionCode1 = 1,
   ConditionCode2 = 2,
   ConditionCode3 = 3,
   ConditionCodeInvalid = 4,
   ConditionCodeLast = ConditionCodeInvalid
   } TR_ConditionCodeNumber;

/**
 * Class Simplifier
 * ================
 *
 * Simple IL tree transformations like constant folding, strength reduction, 
 * canonicalizing expressions (e.g. an add with a constant child will have the 
 * constant as the second child), branch/switch simplification etc. 
 */

class Simplifier : public TR::Optimization
   {

   public:

   Simplifier(TR::OptimizationManager *manager);
   static TR::Optimization *create(TR::OptimizationManager *manager);

   // Simplify the whole method.
   //
   virtual int32_t perform();

   virtual int32_t performOnBlock(TR::Block *);
   virtual void prePerformOnBlocks();
   virtual void postPerformOnBlocks();
   virtual const char * optDetailString() const throw();

   // Simplify a complete block.
   //
   void simplify(TR::Block *block);
   TR::TreeTop * simplifyExtendedBlock(TR::TreeTop *);

   // Convert a conditional branch into either a goto or an unconditional
   // fall-through.
   // Changes the "node" argument to be the replaced branch node, NULL if the branch has been eliminated.
   // Returns true if blocks were removed as a result of the change.
   //
   bool conditionalToUnconditional(TR::Node *&node, TR::Block *block, int takeBranch);

   //
   // These methods should only be used by the simplifier functions
   //

   // Simplify a treetop.
   // Returns the next treetop to be processed.
   //
   TR::TreeTop *simplify(TR::TreeTop *treeTop, TR::Block *block);

   // Simplify an expression sub-tree.
   // Returns the root node, or NULL if the node has been removed
   //
   TR::Node *simplify(TR::Node *node, TR::Block *block);

   void cleanupFlags(TR::Node *node);
   void setCC(TR::Node *n, TR_ConditionCodeNumber cc);
   TR_ConditionCodeNumber getCC(TR::Node *n);

   TR_RegionStructure *containingStructure() { return _containingStructure; }
   bool     reassociate() { return _reassociate; }

   using OMR::Optimization::prepareToReplaceNode;
   void prepareToReplaceNode(TR::Node * node);

   void anchorOrderDependentNodesInSubtree(TR::Node *node, TR::Node *replacement, TR::TreeTop* anchorTree);

   /**
    * Checks IL tree patterns to see if parent-child cancellation can be 
    * done safely.
    * 
    * @param[in] node The tree node that we are trying to unary cancel
    * @param[in] firstChild The first child of node
    * @param[in] opcode The opcode we compare @p firstChild's opcode against
    * @return Returns true if node can be canceled; false otherwise
    */
   virtual bool isLegalToUnaryCancel(TR::Node *node, TR::Node *firstChild, TR::ILOpCodes opcode) 
      { return true; }

   /**
    * Examines node and its first child to see if their operations can cancel 
    * each other out. If so, replaces node with its grandchild.
    * 
    * @param[in/out] node The tree node that we are examining and possibly 
    * changing if appropriate
    * @param[in] firstChild The first child of @p node
    * @param[in] anchorTree The tree to insert node's children in front of  
    * if @p anchorChildren is true
    * @param[in] opcode The opcode we compare @p firstChild's opcode against
    * @param[in] anchorChildren Boolean indicating whether we need to anchor
    * node's children when node is removed. The default is set to true
    * @return Returns the grandchild node if cacenllation can be done; 
    * NULL otherwise
    */
   virtual TR::Node *unaryCancelOutWithChild(TR::Node *node, TR::Node *firstChild, TR::TreeTop *anchorTree, TR::ILOpCodes opcode, bool anchorChildren=true);

   /**
    * Checks IL tree patterns to see if folding can be done safely.
    * 
    * @param[in] node The tree node that we are trying to fold
    * @param[in] firstChild The first child of @p node
    * @return Returns true if node can be folded; false otherwise
    */
   virtual bool isLegalToFold(TR::Node *node, TR::Node *firstChild) 
      { return true; }

   /**
    * Checks if the bound value is definitely greater than or equal to 
    * the length value by examining the IL trees.
    *
    * @param[in] boundChild The tree node corresponding to the bound value
    * @param[in] lengthChild The tree node corresponding to the length value
    * @return Returns true if bound is definitely great than or equal to 
    * length; false otherwise
    */
   virtual bool isBoundDefinitelyGELength(TR::Node *boundChild, TR::Node *lengthChild);

   /**
    * Checks to see if the icall method is a recognized method. If so, 
    * transforms the call into a faster, but functionally equivalent 
    * sequence of IL trees.
    *
    * @param[in/out] node The tree node that is being simplified
    * @param[in] block The block that the node is in
    * @return Returns a transformed node if the method is recognized and 
    * transformations are done; otherwise it returns the original node
    */
   virtual TR::Node *simplifyiCallMethods(TR::Node *node, TR::Block *block) 
      { return node; }

   /**
    * Checks to see if the lcall method is a recognized method. If so, 
    * transforms the call into a faster, but functionally equivalent 
    * sequence of IL trees.
    *
    * @param[in/out] node The tree node that is being simplified
    * @param[in] block The block that the node is in
    * @return Returns a transformed node if the method is recognized and 
    * transformations are done; otherwise it returns the original node
    */
   virtual TR::Node *simplifylCallMethods(TR::Node *node, TR::Block *block) 
      { return node; }

   /**
    * Checks to see if the acall method is a recognized method. If so, 
    * transforms the call into a faster, but functionally equivalent 
    * sequence of IL trees.
    *
    * @param[in/out] node The tree node that is being simplified
    * @param[in] block The block that the node is in
    * @return Returns a transformed node if the method is recognized and 
    * transformations are done; otherwise it returns the original node
    */
   virtual TR::Node *simplifyaCallMethods(TR::Node * node, TR::Block * block) 
      { return node; }

   /**
    * simplifyiOrPatterns is called from iorSimplifier handler to recognize 
    * any additional project-specific IL tree patterns to simplify.
    * 
    * @param[in/out] node The tree node that is being checked and transformed
    * @return Returns a transformed node if the IL tree pattern under node
    * is recognized and transformations are done; NULL otherwise
    */
   virtual TR::Node *simplifyiOrPatterns(TR::Node *node)
      { return NULL; }

   /**
    * simplifyi2sPatterns is called from i2sSimplifier handler to recognize 
    * any additional project-specific IL tree patterns to simplify.
    * 
    * @param[in/out] node The tree node that is being checked and transformed
    * @return Returns a transformed node if the IL tree pattern under node
    * is recognized and transformations are done; NULL otherwise 
    */
   virtual TR::Node *simplifyi2sPatterns(TR::Node *node)
      { return NULL; }

   /**
    * simplifyd2fPatterns is called from d2fSimplifier handler to recognize 
    * any additional project-specific IL tree patterns to simplify.
    * 
    * @param[in/out] node The tree node that is being checked and transformed
    * @return Returns a transformed node if the IL tree pattern under node
    * is recognized and transformations are done; NULL otherwise 
    */
   virtual TR::Node *simplifyd2fPatterns(TR::Node *node)
      { return NULL; }

   /**
    * simplifyIndirectLoadPatterns is called from indirectLoadSimplifier 
    * handler to recognize any additional project-specific IL tree 
    * patterns to simplify.
    * 
    * @param[in/out] node The tree node that is being checked and transformed
    * @return Returns a transformed node if the IL tree pattern under node
    * is recognized and transformations are done; NULL otherwise 
    */
   virtual TR::Node *simplifyIndirectLoadPatterns(TR::Node *node)
      { return NULL; }

   /**
    * This method allows a project to recursively set precision flags 
    * to node and all of its children if it is needed.
    *
    * @param[in] baseNode The tree node whose type determines if we need 
    * to set the precision flags
    * @param[in/out] node The tree node whose precision flag is being modified
    * @param[in/out] visited A check list to keep track of whether a 
    * particular node has been visited already
    */
   virtual void setNodePrecisionIfNeeded(TR::Node *baseNode, TR::Node *node, TR::NodeChecklist &visited)
      {}

   TR::TreeTop         *_curTree;
   TR_UseDefInfo      *_useDefInfo;      // Cached use/def info
   TR_ValueNumberInfo *_valueNumberInfo; // Cached value number info

   bool      _invalidateUseDefInfo;
   bool      _invalidateValueNumberInfo;
   bool      _alteredBlock;
   bool      _blockRemoved;
   bool      _reassociate;
   TR_RegionStructure  *_containingStructure;
   TR_HashTabInt  _hashTable;   // used by reassociation
   TR_HashTabInt  _ccHashTab; // used by zEmulator

   TR::TreeTop      *_performLowerTreeSimplifier;
   TR::Node         *_performLowerTreeNode;
   };

}

#endif
